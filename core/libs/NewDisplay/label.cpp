
#include "FT812.h"

#include "commonControls.h"
#include "display.h"
#include "displayControls.h"

#include <string.h>

cLabel::cLabel(strControlProperties* properties)
  : cDisplayControl(controlIdLabel, 1)
{
    colorsCount = 4;
    colors = display.getTheme().defaultTextColors;

    if (properties == nullptr)
    {
        data = nullptr;
        style2 = 0;
        return;
    }

    colors = (properties->colors == nullptr) ? colors : properties->colors;

    posX = properties->x;
    posY = properties->y;

    text = properties->text;

    value = properties->value;

    data = (strLabelData*)(properties->data);

    width = properties->w;
    height = properties->h;

    setStyle(properties->style);
    setStyle2(properties->style);
}

cLabel::~cLabel()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cLabel::setStyle(uint32_t style)
{
    this->style = style;

    textStyle1 = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                 (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                 (style & controlStyleRightX ? OPT_RIGHTX : 0);

    int8_t textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    font = &fonts[textFont];
}

void cLabel::setStyle2(uint32_t style)
{
    this->style2 = style;

    textStyle2 = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                 (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                 (style & controlStyleRightX ? OPT_RIGHTX : 0);

    int8_t textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    font2 = &fonts[textFont];
}

void cLabel::setText(const char* text)
{
    this->text = text;
}

void cLabel::setText2(const char* text)
{
    this->text2 = text;
}

void cLabel::setValue(int value)
{
    this->value = value;
}

void cLabel::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cLabel::setData(void* data)
{
    this->data = (strLabelData*)(data);
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cLabel::update()
{
    int16_t border_x = posX; //, border_y = posY-2;
    int16_t border_y = posY;

    int16_t lines_step_y = 0;
    int16_t text_x = posX;
    int16_t text_y = posY + (18 - font->height / 2);
    // int16_t text_h = height;

    if (value > 0 && value < 2 && height > font->height)
    {
        // lines_step_y = (text_h - font->height*(value+1)) / (value+2) + font->height;
        lines_step_y = 20;
    }

    if (style & controlStyleCenterX)
    {
        border_x = posX - (width / 2);
    }
    if (style & controlStyleCenterY)
    {
        // border_y = posY - (height/2);

        if (value == 0)
        {
            text_y = posY + (height / 2);
        }
        else
        {
            text_y = (posY + (height / 2)) - ((lines_step_y / 2) * value);
        }
    }

    if (style & controlStyleBackground)
    {
        if (style & controlStyleNoTransparency)
        {
            API_BLEND_FUNC(SRC_ALPHA, ZERO);
        }

        API_COLOR(colors[1]);

        if (style & controlStyleRoundedBorder)
            API_LINE_WIDTH(32);
        else
            API_LINE_WIDTH(16);

        API_BEGIN(RECTS);
        API_VERTEX2F(border_x, border_y);
        API_VERTEX2F(border_x + width, border_y + height);
        API_END();

        if (style & controlStyleBottomShadow)
        {
            API_BLEND_FUNC(SRC_ALPHA, ZERO);

            API_SAVE_CONTEXT();
            uint16_t grad_y = border_y + height - 10;
            uint16_t grad_h = 12;
            API_SCISSOR_XY(border_x, grad_y);
            API_SCISSOR_SIZE(width + 1, grad_h);
            API_CMD_GRADIENT(0, grad_y, colors[1], 0, grad_y + grad_h, 0x0);
            API_RESTORE_CONTEXT();

            //			API_COLOR(colors[0]);
            //			API_LINE_WIDTH(1);
            //			API_BEGIN(LINES);
            //			API_VERTEX2F(border_x-1, border_y);
            //			API_VERTEX2F(border_x-1, border_y+height);
            //			API_VERTEX2F(border_x+width+1, border_y);
            //			API_VERTEX2F(border_x+width+1, border_y+height);
            //			API_END();

            API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        }

        if (style & controlStyleNoTransparency)
        {
            API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        }
    }
    if (style & controlStyleBorder)
    {
        API_COLOR(colors[2]);

        API_LINE_WIDTH(8);

        API_BEGIN(LINE_STRIP);
        API_VERTEX2F(border_x, border_y);
        API_VERTEX2F(border_x + width, border_y);
        API_VERTEX2F(border_x + width, border_y + height);
        API_VERTEX2F(border_x, border_y + height);
        API_VERTEX2F(border_x, border_y);
        API_END();
    }

    if (style & controlStyleVerticalText)
    {
        API_COLOR(colors[0]);
        API_SAVE_CONTEXT();

        API_BITMAP_HANDLE(font->handle);
        API_BEGIN(BITMAPS);

        API_BITMAP_SIZE(NEAREST, BORDER, BORDER, font->height, font->height);

        API_BITMAP_ROTATE_270(font->height);

        uint8_t strPtr = 0;
        int16_t x = posX, y = posY;
        int16_t txtLen = strlen(text);

        if (style & controlStyleCenterX)
        {
            x -= font->height / 2;
        }
        if (style & controlStyleCenterY)
        {
        }

        API_BITMAP_HANDLE(font->handle);
        API_BEGIN(BITMAPS);

        for (uint8_t i = 0; i < txtLen; i++)
        {
            if (x > 799 || y > 479 || x < 0 || y < 0)
            {
                // strPtr++;
            }
            else if ((x > 511 || y > 511) && text[strPtr] >= 32)
            {
                API_CELL(text[strPtr]);
                API_VERTEX2F(x, y);
            }
            else if (text[strPtr] >= 32)
            {
                API_VERTEX2II(x, y, font->handle, (char)text[strPtr]);
            }

            int character = (uint8_t)text[strPtr];
            if (character < 148)
            {
                y -= font->data[character] + 1;
            }
            strPtr++;
        }

        API_END();
        API_RESTORE_CONTEXT();
    }
    /*
     else if (style & controlStyleManualText)
     {
         if (text != nullptr)
         {
             API_COLOR(colors[0]);

             int16_t tempX = posX, tempY = posY;
             int16_t txtLen = strlen(text);

             if (style & controlStyleCenterX)
             {
                 tempX = posX - (txtLen * font->height) / 2;
             }

             if (style & controlStyleCenterX)
             {
                 tempY = posY + ((height / 2) - font->height / 2);
             }

             text_x = tempX;
             text_y = tempY;

             string2Bitmaps(tempX, tempY, text, txtLen, true);
         }
     }
     */
    else
    {
        if (text != nullptr)
        {
            int textLen = 0;

            if (style & controlStyleLimitTextWidth)
            {
                textLen = getMaxTextLengthThatFitSpecificWidth(font, text, width);
            }

            API_COLOR(colors[0]);
            API_CMD_TEXT(posX, text_y, font->handle, textStyle1, text, textLen);

            if (style & controlStyleUnderline && (style & controlStyleVerticalText) == 0)
            {
                drawTextUnderline(text, font, text_x, text_y, textStyle1);
            }
        }
    }

    if (value > 0 && text2 != nullptr && text2[0] != 0)
    {
        text_y = text_y + lines_step_y;

        API_COLOR(colors[3]);
        API_CMD_TEXT(text_x, text_y, font2->handle, textStyle2, text2);

        if (style & controlStyleUnderline && (style & controlStyleVerticalText) == 0)
        {
            drawTextUnderline(text2, font, text_x, text_y, textStyle2);
        }
    }

    API_SAVE_CONTEXT();

    if ((style & controlStyleShowBitmap) && (data != nullptr))
    {
        API_COLOR(colors[0]);

        for (uint8_t i = 0; i < labelBitmapsCount; i++)
        {
            if (data->bitmaps[i].bitmapIndex > 0 &&
                data->bitmaps[i].bitmapIndex < displayBitmapsCount)
            {

                API_CMD_SETBITMAP(bitmaps[data->bitmaps[i].bitmapIndex].address,
                                  bitmaps[data->bitmaps[i].bitmapIndex].format,
                                  bitmaps[data->bitmaps[i].bitmapIndex].width,
                                  bitmaps[data->bitmaps[i].bitmapIndex].height);

                /*				API_BITMAP_HANDLE(0);
                                API_BITMAP_LAYOUT(	bitmaps[data->bitmaps[i].bitmapIndex].format,
                                                    bitmaps[data->bitmaps[i].bitmapIndex].linestride,
                                                    bitmaps[data->bitmaps[i].bitmapIndex].height);

                                API_BITMAP_SIZE(NEAREST, BORDER, BORDER,
                                        bitmaps[data->bitmaps[i].bitmapIndex].width,
                                        bitmaps[data->bitmaps[i].bitmapIndex].height);

                                API_BITMAP_SOURCE(bitmaps[data->bitmaps[i].bitmapIndex].address);
                */

                API_BEGIN(BITMAPS);
                API_VERTEX2F(data->bitmaps[i].xValue -
                               (bitmaps[data->bitmaps[i].bitmapIndex].width / 2),
                             data->bitmaps[i].yValue);
                API_END();
            }
        }
    }

    if (style & controlStyleSpecialFx && data != nullptr)
    {
        API_COLOR(data->line.color);
        API_LINE_WIDTH(8);

        API_BEGIN(LINES);
        API_VERTEX2F(posX + data->line.x1, posY + data->line.y1);
        API_VERTEX2F(posX + data->line.x2, posY + data->line.y2);
        API_END();
    }

    // dots
    if (style & controlStyleSpecialFx2 && data != nullptr)
    {
        constexpr int dotSize = 3;
        constexpr int dotSpacing = 13;
        constexpr int dotBottomOffset = 16;
        int colorIdx = -1;

        API_POINT_SIZE(16 * dotSize);
        API_BEGIN(FTPOINTS);

        if (data->dotIndicator.count > 0)
        {
            int dotsX = posX - (((data->dotIndicator.count - 1) * dotSpacing) / 2);
            int dotsY = posY + height - dotBottomOffset;

            for (int dot = 0; dot < data->dotIndicator.count; dot++)
            {
                int curentColorIdx = (colors[2] != 0 || colors[3] != 0)
                                       ? (data->dotIndicator.current == dot ? 2 : 3)
                                       : (data->dotIndicator.current == dot ? 0 : 1);

                if (colorIdx != curentColorIdx)
                {
                    colorIdx = curentColorIdx;
                    API_COLOR(colors[colorIdx]);
                }

                API_VERTEX2F(dotsX + dot * dotSpacing, dotsY);
            }
        }
        API_END();
    }

    API_RESTORE_CONTEXT();
}

void cLabel::drawTextUnderline(const char* text,
                               const strFont* textFont,
                               uint16_t textX,
                               uint16_t textY,
                               uint16_t textStyle)
{
    textY = textY + textFont->height - (textFont->height / 5);

    int textLen = getTextWidth(textFont, text, strlen(text));

    if (textStyle & OPT_CENTERX)
    {
        textX = textX - textLen / 2;
    }

    API_LINE_WIDTH(8);
    API_BEGIN(LINES);
    API_VERTEX2F(textX, textY);
    API_VERTEX2F(textX + textLen, textY);
    API_END();
}
