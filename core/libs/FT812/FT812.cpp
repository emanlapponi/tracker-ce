/*
********************************************************************************
*             FT81x simple library for PIC MCU                                 *
********************************************************************************
Copyright (c) Future Technology Devices International / BridgeTek

THIS SOFTWARE IS PROVIDED BY FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES LOSS OF USE, DATA, OR PROFITS OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

FTDI DRIVERS MAY BE USED ONLY IN CONJUNCTION WITH PRODUCTS BASED ON FTDI PARTS.

FTDI DRIVERS MAY BE DISTRIBUTED IN ANY FORM AS LONG AS LICENSE INFORMATION IS NOT MODIFIED.

IF A CUSTOM VENDOR ID AND/OR PRODUCT ID OR DESCRIPTION STRING ARE USED, IT IS THE
RESPONSIBILITY OF THE PRODUCT MANUFACTURER TO MAINTAIN ANY CHANGES AND SUBSEQUENT WHQL
RE-CERTIFICATION AS A RESULT OF MAKING THESE CHANGES.

********************************************************************************
* This code is provided as an example only and is not guaranteed by FTDI.
* FTDI accept no responsibility for any issues resulting from its use.
* The developer of the final application incorporating any parts of this
* sample project is responsible for ensuring its safe and correct operation
* and for any consequences resulting from its use.
*
* Rev       Date        Author          Comment
********************************************************************************
* 1.0       June 2017	G Brown         Initial Release
* 1.1       Feb  2018   G Brown         No changes to this file
*  *****************************************************************************
*/

#include "FT812.h"
#include "DisplayList.h"
#include "display.h"

// Coprocessor commands go to the chip's command FIFO. During a capture there
// is no open FIFO transaction, so a command that has no expansion in
// DisplayList is dropped and counted rather than sent.
// A display-list word goes to the list being captured, or else into the
// coprocessor's command stream, which passes it on to the list it builds.
static void listWord(uint32_t word)
{
    if (displayList::capturing())
    {
        displayList::put(word);
        return;
    }
    display.getDisplayDriver()->WriteData32(word);
    display.getDisplayDriver()->IncCMDOffset(4);
}

static void coprocessorWrite(uint32_t word)
{
    if (displayList::capturing())
    {
        displayList::droppedCommands++;
        return;
    }
    display.getDisplayDriver()->WriteData32(word);
}

static void coprocessorAdvance(uint16_t bytes)
{
    if (!displayList::capturing())
    {
        display.getDisplayDriver()->IncCMDOffset(bytes);
    }
}

static uint32_t coprocessorString(const char* string, uint32_t length)
{
    return displayList::capturing() ? 0 : display.getDisplayDriver()->sendString(string, length);
}

//##############################################################################
// Display List commands for co-processor
//##############################################################################

void API_CLEAR_COLOR_RGB(uint8_t R, uint8_t G, uint8_t B)
{
    listWord(CLEAR_COLOR_RGB(R, G, B));
}

void API_CLEAR_COLOR(uint32_t RGB)
{
    listWord((2UL << 24) | (RGB & 0x00FFFFFF));
}

void API_CLEAR(uint8_t C, uint8_t S, uint8_t T)
{
    listWord(CLEAR((C & 0x01), (S & 0x01), (T & 0x01)));
}

void API_COLOR_RGB(uint8_t R, uint8_t G, uint8_t B)
{
    listWord(COLOR_RGB(R, G, B));
}

void API_COLOR(uint32_t RGB)
{
    listWord((4UL << 24) | (RGB & 0x00FFFFFF));
}

void API_VERTEX2F(int16_t x, int16_t y)
{
    listWord(VERTEX2F(x, y));
}

void API_VERTEX2II(uint16_t x, uint16_t y, uint8_t handle, uint8_t cell)
{
    listWord(VERTEX2II(x, y, handle, cell));
}

void API_BITMAP_HANDLE(uint8_t handle)
{
    listWord(BITMAP_HANDLE(handle));
}

void API_BITMAP_SOURCE(uint32_t addr)
{
    listWord(BITMAP_SOURCE(addr));
}

void API_BITMAP_LAYOUT(uint8_t format, uint16_t linestride, uint16_t height)
{
    listWord(BITMAP_LAYOUT(format, linestride, height));
}

void API_BITMAP_SIZE(uint8_t filter, uint8_t wrapx, uint8_t wrapy, uint16_t width, uint16_t height)
{
    listWord(BITMAP_SIZE(filter, wrapx, wrapy, width, height));
}

void API_CELL(uint8_t cell)
{
    listWord(CELL(cell));
}

void API_TAG(uint8_t s)
{
    listWord(TAG(s));
}

void API_ALPHA_FUNC(uint8_t func, uint8_t ref)
{
    listWord(ALPHA_FUNC(func, ref));
}

void API_STENCIL_FUNC(uint8_t func, uint8_t ref, uint8_t mask)
{
    listWord(STENCIL_FUNC(func, ref, mask));
}

void API_BLEND_FUNC(uint8_t src, uint8_t dst)
{
    listWord(BLEND_FUNC(src, dst));
}

void API_STENCIL_OP(uint8_t sfail, uint8_t spass)
{
    listWord(STENCIL_OP(sfail, spass));
}

void API_POINT_SIZE(uint16_t size)
{
    listWord(POINT_SIZE(size));
}

void API_LINE_WIDTH(uint16_t width)
{
    listWord(LINE_WIDTH(width));
}

void API_CLEAR_COLOR_A(uint8_t alpha)
{
    listWord(CLEAR_COLOR_A(alpha));
}

void API_COLOR_A(uint8_t alpha)
{
    listWord(COLOR_A(alpha));
}

void API_CLEAR_STENCIL(uint8_t s)
{
    listWord(CLEAR_STENCIL(s));
}

void API_CLEAR_TAG(uint8_t s)
{
    listWord(CLEAR_TAG(s));
}

void API_STENCIL_MASK(uint8_t mask)
{
    listWord(STENCIL_MASK(mask));
}

void API_TAG_MASK(uint8_t mask)
{
    listWord(TAG_MASK(mask));
}

void API_SCISSOR_XY(uint16_t x, uint16_t y)
{
    listWord(SCISSOR_XY(x, y));
}

void API_SCISSOR_SIZE(uint16_t width, uint16_t height)
{
    listWord(SCISSOR_SIZE(width, height));
}

void API_CALL(uint16_t dest)
{
    listWord(CALL(dest));
}

void API_JUMP(uint16_t dest)
{
    listWord(JUMP(dest));
}

void API_BEGIN(uint8_t prim)
{
    listWord(BEGIN(prim));
}

void API_COLOR_MASK(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    listWord(COLOR_MASK(r, g, b, a));
}

void API_END(void)
{
    listWord(END());
}

void API_SAVE_CONTEXT(void)
{
    listWord(SAVE_CONTEXT());
}

void API_RESTORE_CONTEXT(void)
{
    listWord(RESTORE_CONTEXT());
}

void API_RETURN(void)
{
    listWord(RETURN());
}

void API_MACRO(uint8_t m)
{
    listWord(MACRO(m));
}

void API_DISPLAY(void)
{
    listWord(FT_DISPLAY());
}

//##############################################################################
// Co-Processor Widgets
//##############################################################################

// ft_void_t Ft_Gpu_CoCmd_Text(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// font, ft_uint16_t options, const ft_char8_t* s);
void API_CMD_TEXT(int16_t x,
                  int16_t y,
                  int16_t font,
                  uint16_t options,
                  const char* string,
                  uint32_t length)
{
    if (displayList::capturing())
    {
        displayList::text(x, y, font, options, string, length);
        return;
    }
    uint32_t command_size = 0;
    uint32_t StringLength = 0;

    coprocessorWrite(CMD_TEXT);
    coprocessorWrite(((uint32_t)y << 16) | (x & 0xffff));
    coprocessorWrite(((uint32_t)options << 16) | (font & 0xffff));
    command_size = (command_size + 12);

    StringLength = coprocessorString(string, length);
    command_size = (command_size + StringLength);

    coprocessorAdvance(command_size);
}

void API_CMD_TEXT(int16_t x, int16_t y, int16_t font, uint16_t options, const char* string)
{
    API_CMD_TEXT(x, y, font, options, string, 0);
}

// ft_void_t Ft_Gpu_CoCmd_Button(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// w, ft_int16_t h, ft_int16_t font, ft_uint16_t options, const ft_char8_t* s);
void API_CMD_BUTTON(int16_t x,
                    int16_t y,
                    int16_t w,
                    int16_t h,
                    int16_t font,
                    uint16_t options,
                    const char* string)
{
    uint32_t command_size = 0, StringLength = 0;

    coprocessorWrite(CMD_BUTTON);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)h << 16) | (w & 0xffff)));
    coprocessorWrite((((uint32_t)options << 16) | (font & 0xffff)));
    command_size = (command_size + 16);

    StringLength = coprocessorString(string, 0);
    command_size = (command_size + StringLength);

    coprocessorAdvance(command_size);
}

// ft_void_t Ft_Gpu_CoCmd_Keys(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t w,
// ft_int16_t h, ft_int16_t font, ft_uint16_t options, const ft_char8_t* s);
void API_CMD_KEYS(int16_t x,
                  int16_t y,
                  int16_t w,
                  int16_t h,
                  int16_t font,
                  uint16_t options,
                  const char* string)
{
    //    uint32_t length = strlen(string);
    uint32_t command_size = 0, StringLength = 0;

    coprocessorWrite(CMD_KEYS);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)h << 16) | (w & 0xffff)));
    coprocessorWrite((((uint32_t)options << 16) | (font & 0xffff)));
    command_size = (command_size + 16);

    StringLength = coprocessorString(string, 0);
    command_size = (command_size + StringLength);

    coprocessorAdvance(command_size);
}

// ft_void_t Ft_Gpu_CoCmd_Number(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// font, ft_uint16_t options, ft_int32_t n);
void API_CMD_NUMBER(int16_t x, int16_t y, int16_t font, uint16_t options, int32_t n)
{
    if (displayList::capturing())
    {
        displayList::number(x, y, font, options, n);
        return;
    }
    coprocessorWrite(CMD_NUMBER);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)options << 16) | (font & 0xffff)));
    coprocessorWrite(n);
    coprocessorAdvance(16);
}

/*
void API_CMD_NUMBER_2(int16_t x, int16_t y, int16_t font, uint16_t options, int32_t n)
{
    if()


    coprocessorWrite(CMD_NUMBER);
    coprocessorWrite( (((uint32_t)y<<16)|(x & 0xffff)));
    coprocessorWrite( (((uint32_t)options<<16)|(font&0xffff)));
    coprocessorWrite( n);
    coprocessorAdvance(16);


}
*/

// ft_void_t Ft_Gpu_CoCmd_LoadIdentity(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_LOADIDENTITY(void)
{
    coprocessorWrite(CMD_LOADIDENTITY);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_Toggle(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// w, ft_int16_t font, ft_uint16_t options, ft_uint16_t state, const ft_char8_t* s);
void API_CMD_TOGGLE(int16_t x,
                    int16_t y,
                    int16_t w,
                    int16_t font,
                    uint16_t options,
                    uint16_t state,
                    const char* string)
{
    // uint32_t length = strlen(string);
    uint32_t command_size = 0, StringLength = 0;

    coprocessorWrite(CMD_TOGGLE);
    coprocessorWrite(((uint32_t)y << 16) | (x & 0xffff));
    coprocessorWrite(((uint32_t)font << 16) | (w & 0xffff));
    coprocessorWrite(((uint32_t)state << 16) | options);
    command_size = (command_size + 16);

    StringLength = coprocessorString(string, 0);
    command_size = (command_size + StringLength);

    coprocessorAdvance(command_size);
}

/* Error handling for val is not done, so better to always use range of 65535 in order that needle
 * is drawn within display region */
// ft_void_t Ft_Gpu_CoCmd_Gauge(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// r, ft_uint16_t options, ft_uint16_t major, ft_uint16_t minor, ft_uint16_t val, ft_uint16_t
// range);
void API_CMD_GAUGE(int16_t x,
                   int16_t y,
                   int16_t r,
                   uint16_t options,
                   uint16_t major,
                   uint16_t minor,
                   uint16_t val,
                   uint16_t range)
{
    coprocessorWrite(CMD_GAUGE);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)options << 16) | (r & 0xffff)));
    coprocessorWrite((((uint32_t)minor << 16) | (major & 0xffff)));
    coprocessorWrite((((uint32_t)range << 16) | (val & 0xffff)));
    coprocessorAdvance(20);
}

// ft_void_t Ft_Gpu_CoCmd_RegRead(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t result);
void API_CMD_REGREAD(uint32_t ptr, uint32_t result)
{
    coprocessorWrite(CMD_REGREAD);
    coprocessorWrite(ptr);
    coprocessorWrite(0);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_GetProps(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t w,
// ft_uint32_t h);
void API_CMD_GETPROPS(uint32_t ptr, uint32_t w, uint32_t h)
{
    coprocessorWrite(CMD_GETPROPS);
    coprocessorWrite(ptr);
    coprocessorWrite(w);
    coprocessorWrite(h);
    coprocessorAdvance(16);
}

// ft_void_t Ft_Gpu_CoCmd_Memcpy(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t dest, ft_uint32_t src,
// ft_uint32_t num);
void API_CMD_MEMCPY(uint32_t dest, uint32_t src, uint32_t num)
{
    coprocessorWrite(CMD_MEMCPY);
    coprocessorWrite(dest);
    coprocessorWrite(src);
    coprocessorWrite(num);
    coprocessorAdvance(16);
}

// ft_void_t Ft_Gpu_CoCmd_Spinner(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y,
// ft_uint16_t style, ft_uint16_t scale);
void API_CMD_SPINNER(int16_t x, int16_t y, uint16_t style, uint16_t scale)
{
    coprocessorWrite(CMD_SPINNER);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)scale << 16) | (style & 0xffff)));
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_BgColor(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t c);
void API_CMD_BGCOLOR(uint32_t c)
{
    coprocessorWrite(CMD_BGCOLOR);
    coprocessorWrite(c);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_Swap(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_SWAP(void)
{
    displayList::coprocessorDrew = true;
    coprocessorWrite(CMD_SWAP);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_Inflate(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr);
void API_CMD_INFLATE(uint32_t ptr)
{
    coprocessorWrite(CMD_INFLATE);
    coprocessorWrite(ptr);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_Translate(Ft_Gpu_Hal_Context_t *phost,ft_int32_t tx, ft_int32_t ty);
void API_CMD_TRANSLATE(int32_t tx, int32_t ty)
{
    coprocessorWrite(CMD_TRANSLATE);
    coprocessorWrite(tx);
    coprocessorWrite(ty);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_Stop(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_STOP(void)
{
    coprocessorWrite(CMD_STOP);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_Slider(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// w, ft_int16_t h, ft_uint16_t options, ft_uint16_t val, ft_uint16_t range);
void API_CMD_SLIDER(int16_t x,
                    int16_t y,
                    int16_t w,
                    int16_t h,
                    uint16_t options,
                    uint16_t val,
                    uint16_t range)
{
    coprocessorWrite(CMD_SLIDER);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)h << 16) | (w & 0xffff)));
    coprocessorWrite((((uint32_t)val << 16) | (options & 0xffff)));
    coprocessorWrite(range);
    coprocessorAdvance(20);
}

void API_BITMAP_ROTATE_270(uint16_t height)
{
    if (displayList::capturing())
    {
        displayList::rotate270(height);
        return;
    }
    const uint32_t side = 2u * (height / 2);
    listWord(BITMAP_TRANSFORM_F(0));
    listWord(BITMAP_TRANSFORM_E(0));
    listWord(BITMAP_TRANSFORM_D(0xFF));
    listWord(BITMAP_TRANSFORM_C(side * 256 - 1));
    listWord(BITMAP_TRANSFORM_B(0x1FF00));
    listWord(BITMAP_TRANSFORM_A(0));
}

void API_BITMAP_TRANSFORM_A(long a)
{
    listWord(BITMAP_TRANSFORM_A(a)); //    ((21UL<<24)|(((a)&131071UL)<<0))
}
void API_BITMAP_TRANSFORM_B(long b)
{
    listWord(BITMAP_TRANSFORM_B(b)); //  ((22UL<<24)|(((b)&131071UL)<<0))
}
void API_BITMAP_TRANSFORM_C(long c)
{
    listWord(BITMAP_TRANSFORM_C(c)); //  ((23UL<<24)|(((c)&16777215UL)<<0))
}
void API_BITMAP_TRANSFORM_D(long d)
{
    listWord(BITMAP_TRANSFORM_D(d)); //   ((24UL<<24)|(((d)&131071UL)<<0))
}
void API_BITMAP_TRANSFORM_E(long e)
{
    listWord(BITMAP_TRANSFORM_E(e)); //   ((25UL<<24)|(((e)&131071UL)<<0))
}
void API_BITMAP_TRANSFORM_F(long f)
{
    listWord(BITMAP_TRANSFORM_F(f)); //  ((26UL<<24)|(((f)&16777215UL)<<0))
}

// ft_void_t Ft_Gpu_CoCmd_Interrupt(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ms);
void API_CMD_INTERRUPT(uint32_t ms)
{
    coprocessorWrite(CMD_INTERRUPT);
    coprocessorWrite(ms);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_FgColor(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t c);
void API_CMD_FGCOLOR(uint32_t c)
{
    coprocessorWrite(CMD_FGCOLOR);
    coprocessorWrite(c);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_Rotate(Ft_Gpu_Hal_Context_t *phost,ft_int32_t a);
void API_CMD_ROTATE(int32_t a)
{
    coprocessorWrite(CMD_ROTATE);
    coprocessorWrite(a);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_MemWrite(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t num);
void API_CMD_MEMWRITE(uint32_t ptr, uint32_t num)
{
    coprocessorWrite(CMD_MEMWRITE);
    coprocessorWrite(ptr);
    coprocessorWrite(num);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_Scrollbar(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y,
// ft_int16_t w, ft_int16_t h, ft_uint16_t options, ft_uint16_t val, ft_uint16_t size, ft_uint16_t
// range);
void API_CMD_SCROLLBAR(int16_t x,
                       int16_t y,
                       int16_t w,
                       int16_t h,
                       uint16_t options,
                       uint16_t val,
                       uint16_t size,
                       uint16_t range)
{
    coprocessorWrite(CMD_SCROLLBAR);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)h << 16) | (w & 0xffff)));
    coprocessorWrite((((uint32_t)val << 16) | (options & 0xffff)));
    coprocessorWrite((((uint32_t)range << 16) | (size & 0xffff)));
    coprocessorAdvance(20);
}

// ft_void_t Ft_Gpu_CoCmd_GetMatrix(Ft_Gpu_Hal_Context_t *phost,ft_int32_t a, ft_int32_t b,
// ft_int32_t c, ft_int32_t d, ft_int32_t e, ft_int32_t f);
void API_CMD_GETMATRIX(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f)
{
    coprocessorWrite(CMD_GETMATRIX);
    coprocessorWrite(a);
    coprocessorWrite(b);
    coprocessorWrite(c);
    coprocessorWrite(d);
    coprocessorWrite(e);
    coprocessorWrite(f);
    coprocessorAdvance(28);
}

// ft_void_t Ft_Gpu_CoCmd_Sketch(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_uint16_t
// w, ft_uint16_t h, ft_uint32_t ptr, ft_uint16_t format);
void API_CMD_SKETCH(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t ptr, uint16_t format)
{
    coprocessorWrite(CMD_SKETCH);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)h << 16) | (w & 0xffff)));
    coprocessorWrite(ptr);
    coprocessorWrite(format);
    coprocessorAdvance(20);
}

// ft_void_t Ft_Gpu_CoCmd_MemSet(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t value,
// ft_uint32_t num);
void API_CMD_MEMSET(uint32_t ptr, uint32_t value, uint32_t num)
{
    coprocessorWrite(CMD_MEMSET);
    coprocessorWrite(ptr);
    coprocessorWrite(value);
    coprocessorWrite(num);
    coprocessorAdvance(16);
}

// ft_void_t Ft_Gpu_CoCmd_GradColor(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t c);
void API_CMD_GRADCOLOR(uint32_t c)
{
    coprocessorWrite(CMD_GRADCOLOR);
    coprocessorWrite(c);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_Bitmap_Transform(Ft_Gpu_Hal_Context_t *phost,ft_int32_t x0, ft_int32_t y0,
// ft_int32_t x1, ft_int32_t y1, ft_int32_t x2, ft_int32_t y2, ft_int32_t tx0, ft_int32_t ty0,
// ft_int32_t tx1, ft_int32_t ty1, ft_int32_t tx2, ft_int32_t ty2, ft_uint16_t result);
void API_CMD_BITMAP_TRANSFORM(int32_t x0,
                              int32_t y0,
                              int32_t x1,
                              int32_t y1,
                              int32_t x2,
                              int32_t y2,
                              int32_t tx0,
                              int32_t ty0,
                              int32_t tx1,
                              int32_t ty1,
                              int32_t tx2,
                              int32_t ty2,
                              uint16_t result)
{
    coprocessorWrite(CMD_BITMAP_TRANSFORM);
    coprocessorWrite(x0);
    coprocessorWrite(y0);
    coprocessorWrite(x1);
    coprocessorWrite(y1);
    coprocessorWrite(x2);
    coprocessorWrite(y2);
    coprocessorWrite(tx0);
    coprocessorWrite(ty0);
    coprocessorWrite(tx1);
    coprocessorWrite(ty1);
    coprocessorWrite(tx2);
    coprocessorWrite(ty2);
    coprocessorWrite(result);
    coprocessorAdvance(56);
}

// ft_void_t Ft_Gpu_CoCmd_Calibrate(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t result);
void API_CMD_CALIBRATE(uint32_t result)
{
    coprocessorWrite(CMD_CALIBRATE);
    coprocessorWrite(result);
    coprocessorAdvance(8);
    // Ft_Gpu_Hal_WaitCmdfifo_empty(phost);
}

// ft_void_t Ft_Gpu_CoCmd_SetFont(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t font, ft_uint32_t ptr);
void API_CMD_SETFONT(uint32_t font, uint32_t ptr)
{
    coprocessorWrite(CMD_SETFONT);
    coprocessorWrite(font);
    coprocessorWrite(ptr);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_Logo(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_LOGO(void)
{
    coprocessorWrite(CMD_LOGO);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_Append(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t num);
void API_CMD_APPEND(uint32_t ptr, uint32_t num)
{
    coprocessorWrite(CMD_APPEND);
    coprocessorWrite(ptr);
    coprocessorWrite(num);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_MemZero(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t num);
void API_CMD_MEMZERO(uint32_t ptr, uint32_t num)
{
    coprocessorWrite(CMD_MEMZERO);
    coprocessorWrite(ptr);
    coprocessorWrite(num);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_Scale(Ft_Gpu_Hal_Context_t *phost,ft_int32_t sx, ft_int32_t sy);
void API_CMD_SCALE(int32_t sx, int32_t sy)
{
    coprocessorWrite(CMD_SCALE);
    coprocessorWrite(sx);
    coprocessorWrite(sy);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_Clock(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// r, ft_uint16_t options, ft_uint16_t h, ft_uint16_t m, ft_uint16_t s, ft_uint16_t ms);
void API_CMD_CLOCK(int16_t x,
                   int16_t y,
                   int16_t r,
                   uint16_t options,
                   uint16_t h,
                   uint16_t m,
                   uint16_t s,
                   uint16_t ms)
{
    coprocessorWrite(CMD_CLOCK);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)options << 16) | (r & 0xffff)));
    coprocessorWrite((((uint32_t)m << 16) | (h & 0xffff)));
    coprocessorWrite((((uint32_t)ms << 16) | (s & 0xffff)));
    coprocessorAdvance(20);
}

// ft_void_t Ft_Gpu_CoCmd_Gradient(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x0, ft_int16_t y0,
// ft_uint32_t rgb0, ft_int16_t x1, ft_int16_t y1, ft_uint32_t rgb1);
void API_CMD_GRADIENT(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1)
{
    if (displayList::capturing())
    {
        displayList::gradient(x0, y0, rgb0, x1, y1, rgb1);
        return;
    }
    coprocessorWrite(CMD_GRADIENT);
    coprocessorWrite((((uint32_t)y0 << 16) | (x0 & 0xffff)));
    coprocessorWrite(rgb0);
    coprocessorWrite((((uint32_t)y1 << 16) | (x1 & 0xffff)));
    coprocessorWrite(rgb1);
    coprocessorAdvance(20);
}

// ft_void_t Ft_Gpu_CoCmd_SetMatrix(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_SETMATRIX(void)
{
    coprocessorWrite(CMD_SETMATRIX);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_Track(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// w, ft_int16_t h, ft_int16_t tag);
void API_CMD_TRACK(int16_t x, int16_t y, int16_t w, int16_t h, int16_t tag)
{
    coprocessorWrite(CMD_TRACK);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)h << 16) | (w & 0xffff)));
    coprocessorWrite(tag);
    coprocessorAdvance(16);
}

// ft_void_t Ft_Gpu_CoCmd_GetPtr(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t result);
void API_CMD_GETPTR(uint32_t result)
{
    coprocessorWrite(CMD_GETPTR);
    coprocessorWrite(result);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_Progress(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y,
// ft_int16_t w, ft_int16_t h, ft_uint16_t options, ft_uint16_t val, ft_uint16_t range);
void API_CMD_PROGRESS(int16_t x,
                      int16_t y,
                      int16_t w,
                      int16_t h,
                      uint16_t options,
                      uint16_t val,
                      uint16_t range)
{
    coprocessorWrite(CMD_PROGRESS);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)h << 16) | (w & 0xffff)));
    coprocessorWrite((((uint32_t)val << 16) | (options & 0xffff)));
    coprocessorWrite(range);
    coprocessorAdvance(20);
}

// ft_void_t Ft_Gpu_CoCmd_ColdStart(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_COLDSTART(void)
{
    coprocessorWrite(CMD_COLDSTART);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_Dial(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t r,
// ft_uint16_t options, ft_uint16_t val);
void API_CMD_DIAL(int16_t x, int16_t y, int16_t r, uint16_t options, uint16_t val)
{
    coprocessorWrite(CMD_DIAL);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)options << 16) | (r & 0xffff)));
    coprocessorWrite(val);
    coprocessorAdvance(16);
}

// ft_void_t Ft_Gpu_CoCmd_LoadImage(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t
// options);
void API_CMD_LOADIMAGE(uint32_t ptr, uint32_t options)
{
    coprocessorWrite(CMD_LOADIMAGE);
    coprocessorWrite(ptr);
    coprocessorWrite(options);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_Dlstart(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_DLSTART(void)
{
    displayList::coprocessorDrew = true;
    coprocessorWrite(CMD_DLSTART);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_Snapshot(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr);
void API_CMD_SNAPSHOT(uint32_t ptr)
{
    coprocessorWrite(CMD_SNAPSHOT);
    coprocessorWrite(ptr);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_ScreenSaver(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_SCREENSAVER(void)
{
    coprocessorWrite(CMD_SCREENSAVER);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_Memcrc(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t num,
// ft_uint32_t result);
void API_CMD_MEMCRC(uint32_t ptr, uint32_t num, uint32_t result)
{
    coprocessorWrite(CMD_MEMCRC);
    coprocessorWrite(ptr);
    coprocessorWrite(num);
    coprocessorWrite(result);
    coprocessorAdvance(16);
}

// FT81X-only features
#ifdef FT_81X_ENABLE

// ###############       GPU        ############################################

void API_VERTEX_FORMAT(uint8_t frac)
{
    listWord(VERTEX_FORMAT(frac));
}

void API_BITMAP_LAYOUT_H(uint8_t linestride, uint8_t height)
{
    listWord(BITMAP_LAYOUT_H(linestride, height));
}

void API_BITMAP_SIZE_H(uint8_t width, uint8_t height)
{
    listWord(BITMAP_SIZE_H(width, height));
}

void API_PALETTE_SOURCE(uint32_t addr)
{
    listWord(PALETTE_SOURCE(addr));
}

void API_VERTEX_TRANSLATE_X(uint32_t x)
{
    listWord(VERTEX_TRANSLATE_X(x));
}

void API_VERTEX_TRANSLATE_Y(uint32_t y)
{
    listWord(VERTEX_TRANSLATE_Y(y));
}

void API_NOP(void)
{
    listWord(NOP());
}

// ###############       CO-PRO        #########################################

// ft_void_t Ft_Gpu_CoCmd_SetRotate(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t r);
void API_CMD_SETROTATE(uint32_t r)
{
    coprocessorWrite(CMD_SETROTATE);
    coprocessorWrite(r);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_SetFont2(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t font, ft_uint32_t ptr,
// ft_uint32_t firstchar);
void API_CMD_SETFONT2(uint32_t font, uint32_t ptr, uint32_t firstchar)
{
    coprocessorWrite(CMD_SETFONT2);
    coprocessorWrite(font);
    coprocessorWrite(ptr);
    coprocessorWrite(firstchar);
    coprocessorAdvance(16);
}

// ft_void_t Ft_Gpu_CoCmd_Snapshot2(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t fmt, ft_uint32_t ptr,
// ft_int16_t x, ft_int16_t y, ft_int16_t w, ft_int16_t h);
void API_CMD_SNAPSHOT2(uint32_t fmt, uint32_t ptr, int16_t x, int16_t y, int16_t w, int16_t h)
{
    coprocessorWrite(CMD_SNAPSHOT2);
    coprocessorWrite(fmt);
    coprocessorWrite(ptr);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)h << 16) | (w & 0xffff)));
    coprocessorAdvance(20);
}

// ft_void_t Ft_Gpu_CoCmd_MediaFifo(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t size);
void API_CMD_MEDIAFIFO(uint32_t ptr, uint32_t size)
{
    coprocessorWrite(CMD_MEDIAFIFO);
    coprocessorWrite(ptr);
    coprocessorWrite(size);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_Int_SWLoadImage(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t
// options);
void API_CMD_INT_SWLOADIMAGE(uint32_t ptr, uint32_t options)
{
    coprocessorWrite(CMD_INT_SWLOADIMAGE);
    coprocessorWrite(ptr);
    coprocessorWrite(options);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_Sync(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_SYNC(void)
{
    coprocessorWrite(CMD_SYNC);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_CSketch(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y,
// ft_uint16_t w, ft_uint16_t h, ft_uint32_t ptr, ft_uint16_t format,ft_uint16_t freq);
void API_CMD_CSKETCH(int16_t x,
                     int16_t y,
                     uint16_t w,
                     uint16_t h,
                     uint32_t ptr,
                     uint16_t format,
                     uint16_t freq)
{
    coprocessorWrite(CMD_CSKETCH);
    coprocessorWrite((((uint32_t)y << 16) | (x & 0xffff)));
    coprocessorWrite((((uint32_t)h << 16) | (w & 0xffff)));
    coprocessorWrite(ptr);
    coprocessorWrite((((uint32_t)freq << 16) | (format & 0xffff)));
    coprocessorAdvance(20);
}

// ft_void_t Ft_Gpu_CoCmd_RomFont(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t font, ft_uint32_t
// romslot);
void API_CMD_ROMFONT(uint32_t font, uint32_t romslot)
{
    coprocessorWrite(CMD_ROMFONT);
    coprocessorWrite(font);
    coprocessorWrite(romslot);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_PlayVideo(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t options);
void API_CMD_PLAYVIDEO(uint32_t options)
{
    coprocessorWrite(CMD_PLAYVIDEO);
    coprocessorWrite(options);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_VideoFrame(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t dst, ft_uint32_t ptr);
void API_CMD_VIDEOFRAME(uint32_t dst, uint32_t ptr)
{
    coprocessorWrite(CMD_VIDEOFRAME);
    coprocessorWrite(dst);
    coprocessorWrite(ptr);
    coprocessorAdvance(12);
}

// ft_void_t Ft_Gpu_CoCmd_VideoStart(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_VIDEOSTART(void)
{
    coprocessorWrite(CMD_VIDEOSTART);
    coprocessorAdvance(4);
}

// ft_void_t Ft_Gpu_CoCmd_SetBase(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t base);
void API_CMD_SETBASE(uint32_t base)
{
    // Kept for both paths: the base outlives the list it was set in.
    displayList::setBase(base);
    if (displayList::capturing())
    {
        return;
    }
    coprocessorWrite(CMD_SETBASE);
    coprocessorWrite(base);
    coprocessorAdvance(8);
}

// ft_void_t Ft_Gpu_CoCmd_SetBitmap(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t source, ft_uint16_t fmt,
// ft_uint16_t w, ft_uint16_t h);
void API_CMD_SETBITMAP(uint32_t source, uint16_t fmt, uint16_t w, uint16_t h)
{
    if (displayList::capturing())
    {
        displayList::setBitmap(source, fmt, w, h);
        return;
    }
    coprocessorWrite(CMD_SETBITMAP);
    coprocessorWrite(source);
    coprocessorWrite((((uint32_t)w << 16) | (fmt & 0xffff)));
    coprocessorWrite(h);
    coprocessorAdvance(16);
}

// ft_void_t Ft_Gpu_CoCmd_SetScratch(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t handle);
void API_CMD_SETSCRATCH(uint32_t handle)
{
    coprocessorWrite(CMD_SETSCRATCH);
    coprocessorWrite(handle);
    coprocessorAdvance(8);
}
#endif // FT81X-only features
