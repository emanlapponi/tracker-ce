#ifndef LIBS_FT812_DISPLAYLIST_H_
#define LIBS_FT812_DISPLAYLIST_H_

#include <stdint.h>

// Display lists built in the MCU's memory.
//
// While a capture is open, the API_* drawing calls append display-list words
// to the given buffer instead of sending commands to the chip's coprocessor.
// The coprocessor commands the controls use (text, number, gradient, bitmap
// setup) are expanded here into the same words the chip would produce, so a
// captured list can be written straight to the chip's display-list memory.
namespace displayList
{
constexpr uint32_t kFontMetricsSize = 256;

// Returns the first kFontMetricsSize bytes of a font's metric block (the
// widths of characters 0-127, then format, stride, width and height), or
// nullptr for an unknown handle. The chip indexes the widths with the full
// character code, so it reads past the 128 widths for codes above 127; to
// place such text the same way the block has to be that long.
typedef const uint8_t* (*FontMetrics)(uint8_t handle);
void setFontMetrics(FontMetrics lookup);

void capture(uint32_t* buffer, uint32_t capacityWords);
// Ends the capture and returns the number of words written. A list that did
// not fit is discarded as a whole (returns 0), because a truncated one would
// leave the graphics context unbalanced for whatever is drawn after it.
uint32_t finish();
bool capturing();

// The functions below write to the open capture and do nothing without one.
void put(uint32_t word);
void text(int16_t x,
          int16_t y,
          int16_t font,
          uint16_t options,
          const char* string,
          uint32_t length);
void number(int16_t x, int16_t y, int16_t font, uint16_t options, int32_t n);
void setBase(uint32_t base);
void gradient(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1);
void setBitmap(uint32_t source, uint16_t format, uint16_t width, uint16_t height);
// Bitmap transform that turns a glyph of the given height a quarter turn
// counter-clockwise, as the coprocessor computes it for a rotation about the
// glyph's centre.
void rotate270(uint16_t height);

// Coprocessor commands called during a capture that have no expansion here.
extern uint32_t droppedCommands;
extern uint32_t overflows;
// Set when the coprocessor was told to build or show a display list of its
// own, which invalidates what the display code knows about the chip's memory.
extern bool coprocessorDrew;
} // namespace displayList

#endif
