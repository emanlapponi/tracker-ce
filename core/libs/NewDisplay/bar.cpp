#include "FT812.h"

#include "barControl.h"
#include "display.h"
#include "displayControls.h"

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
cBar::cBar(strControlProperties* properties)
  : cDisplayControl(controlIdBar, 1)
{
    colorsCount = 5;
    colors = display.getTheme().activeBarColors;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        text = nullptr;
        data = nullptr;
        value = 0;
        width = 0;
        height = 0;
        style = 0;
        return;
    }

    colors = (properties->colors == nullptr) ? colors : properties->colors;

    posX = properties->x;
    posY = properties->y;

    text = properties->text;
    value = properties->value;

    width = properties->w;
    height = properties->h;

    data = (strBarData*)(properties->data);

    setStyle(properties->style);
}

cBar::~cBar()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cBar::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    int8_t fontIdx = FONT_INDEX_FROM_STYLE;
    fontIdx = (fontIdx >= 0) ? fontIdx : 0;
    font = &fonts[fontIdx];
    textFont = font->handle;
}

void cBar::setText(const char* text)
{
    this->text = text;
}

void cBar::setValue(int value)
{
    this->value = value;
}

void cBar::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cBar::setData(void* data)
{
    data = (strBarData*)(data);
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cBar::update()
{
    uint16_t barWidth = /*(width >= 800/8) ? 800/8-20 : */ width - 20;
    uint16_t barX = posX + (width - barWidth) / 2; //(width > 800/8) ? posX+5 : posX + width/2;
    uint16_t barY = posY + 10;
    uint16_t barHeight = height - 25;

    if (style & controlStyleBackground)
    {
        API_COLOR(colors[4]);
        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(posX, posY);
        API_VERTEX2F(posX + width, posY + height);
        API_END();

        if (style & controlStyleBottomShadow)
        {
            API_BLEND_FUNC(SRC_ALPHA, ZERO);

            API_SAVE_CONTEXT();
            uint16_t grad_y = posY + height - 10;
            uint16_t grad_h = 12;
            API_SCISSOR_XY(posX, grad_y);
            API_SCISSOR_SIZE(width + 1, grad_h);
            API_CMD_GRADIENT(0, grad_y, colors[4], 0, grad_y + grad_h, 0x0);
            API_RESTORE_CONTEXT();

            API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        }
    }

    if (style & controlStyleValue_0_100 && value < 0)
    {
    }
    else // jesli ma pokazywac jakas wartosc to rysowac
    {
        // kontener
        API_COLOR(colors[3]);
        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(posX + 10, posY + 11);
        API_VERTEX2F(posX + width - 10, posY + height - 14);
        API_END();

        if (style & controlStyleGradient)
        {
            API_SAVE_CONTEXT();
            API_SCISSOR_XY(posX + 10, posY + 11);
            API_SCISSOR_SIZE(8, height - 25);
            API_CMD_GRADIENT(posX + 10, 0, 0x0, posX + 18, 0, colors[3]);
            API_RESTORE_CONTEXT();
        }
    }

    if (style & controlStyleVerticalText)
    {
        if (text != nullptr && text[0] != '\0')
        {
            const strFont* drawFont = (font != nullptr) ? font : &fonts[1];
            const uint16_t side = 2u * (drawFont->height / 2u);
            const int16_t maxTextPx =
              (barHeight > 24) ? static_cast<int16_t>(barHeight - 24) : static_cast<int16_t>(barHeight);

            int16_t totalPx = 0;
            uint8_t drawLen = 0;
            for (uint8_t i = 0; text[i] != '\0'; i++)
            {
                uint8_t ch = static_cast<uint8_t>(text[i]);
                int16_t adv = (ch < 148) ? (drawFont->data[ch] + 1) : (drawFont->height / 2);
                if (totalPx + adv > maxTextPx)
                {
                    break;
                }
                totalPx += adv;
                drawLen++;
            }

            if (drawLen > 0)
            {
                API_COLOR(colors[0]);
                API_SAVE_CONTEXT();

                API_BITMAP_HANDLE(drawFont->handle);
                API_BEGIN(BITMAPS);
                API_BITMAP_SIZE(NEAREST, BORDER, BORDER, drawFont->height, drawFont->height);
                API_BITMAP_ROTATE_270(drawFont->height);

                int16_t x = posX + (width / 2) - (drawFont->height / 2);
                int16_t y = barY + (barHeight / 2) + (totalPx / 2) - side;

                API_BITMAP_HANDLE(drawFont->handle);
                API_BEGIN(BITMAPS);

                for (uint8_t i = 0; i < drawLen; i++)
                {
                    uint8_t ch = static_cast<uint8_t>(text[i]);
                    if (x >= 0 && x <= 799 && y >= 0 && y <= 479 && ch >= 32)
                    {
                        if (x > 511 || y > 511)
                        {
                            API_CELL(ch);
                            API_VERTEX2F(x, y);
                        }
                        else
                        {
                            API_VERTEX2II(x, y, drawFont->handle, static_cast<char>(ch));
                        }
                    }
                    if (ch < 148)
                    {
                        y -= drawFont->data[ch] + 1;
                    }
                }

                API_END();
                API_RESTORE_CONTEXT();
            }
        }
        return;
    }

    if (style & controlStyleCompareTwoValues && value >= 0 && value <= 100 && data->value >= 0 &&
        data->value <= 100)
    {
        uint16_t barFillY = barHeight - (barHeight * value) / 100;
        int8_t valueSub = data->value - value;
        int8_t absValueSub = (valueSub >= 0) ? valueSub : -valueSub;
        uint16_t compareBarHeight = (barHeight * absValueSub) / 100;

        API_COLOR(colors[0]);

        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(barX + 1,
                     (valueSub < 0) ? barY + barFillY + compareBarHeight : barY + barFillY);
        API_VERTEX2F(barX + barWidth - 1, barY + barHeight);

        if (valueSub > 0)
        {
            API_COLOR(colors[1]);
            // API_BEGIN(RECTS);
            API_VERTEX2F(barX, barY + barFillY);
            API_VERTEX2F(barX + barWidth - 1, barY + barFillY - compareBarHeight);
        }
        else if (valueSub < 0)
        {
            API_COLOR(colors[2]);
            // API_BEGIN(RECTS);
            API_VERTEX2F(barX + barWidth - 1, barY + barFillY + compareBarHeight);
            API_VERTEX2F(barX + 1, barY + barFillY);
        }

        API_END();
    }
    if (style & controlStyleValue_0_100 && value >= 1000 && value < 1256)
    {
        uint8_t wtBend = (uint8_t)(value - 1000);
        uint8_t nx = (wtBend >> 4) & 0x0F;
        int vx = (nx <= 7) ? (int)nx : ((nx >= 9) ? (int)nx - 16 : 0);
        uint8_t ny = wtBend & 0x0F;
        int vy = (ny <= 7) ? (int)ny : ((ny >= 9) ? (int)ny - 16 : 0);

        int valX = (vx * 100) / 7;
        int valY = (vy * 100) / 7;

        uint16_t subWidth = (barWidth - 5) / 2;
        uint16_t leftX = barX;
        uint16_t rightX = barX + barWidth - subWidth;

        API_COLOR(colors[4]);
        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(leftX + subWidth, posY + 11);
        API_VERTEX2F(rightX - 1, posY + height - 14);
        API_END();

        if (style & controlStyleGradient)
        {
            API_SAVE_CONTEXT();
            API_SCISSOR_XY(rightX, posY + 11);
            API_SCISSOR_SIZE(8, height - 25);
            API_CMD_GRADIENT(rightX, 0, 0x0, rightX + 8, 0, colors[3]);
            API_RESTORE_CONTEXT();
        }

        uint16_t halfFill = barHeight / 2;
        uint16_t topX = (barY + 1) + (valX > 0 ? ((halfFill * (100 - valX)) / 100) : halfFill);
        uint16_t botX = barY + barHeight - (valX < 0 ? ((halfFill * (100 + valX)) / 100) : halfFill);

        uint16_t topY = (barY + 1) + (valY > 0 ? ((halfFill * (100 - valY)) / 100) : halfFill);
        uint16_t botY = barY + barHeight - (valY < 0 ? ((halfFill * (100 + valY)) / 100) : halfFill);

        API_COLOR(colors[0]);
        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(leftX + 1, topX);
        API_VERTEX2F(leftX + subWidth - 1, botX);
        API_VERTEX2F(rightX + 1, topY);
        API_VERTEX2F(rightX + subWidth - 1, botY);
        API_END();
    }
    else if (style & controlStyleValue_0_100 && value >= 0 && value <= 100)
    {
        uint16_t barFillY = barHeight - (barHeight * value) / 100;

        if (data != nullptr && data->changingColor)
            API_COLOR(data->value);
        else
            API_COLOR(colors[0]);

        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(barX + 1, (barY + barFillY));
        API_VERTEX2F((barX + barWidth - 1), (barY + barHeight));
        API_END();
    }
    else if (style & controlStyleReverse_0_100 && value >= 0 && value <= 100)
    {
        uint16_t barFillY = (barHeight * value) / 100;

        if (data != nullptr && data->changingColor)
            API_COLOR(data->value);
        else
            API_COLOR(colors[0]);

        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(barX + 1, (barY + barFillY));
        API_VERTEX2F((barX + barWidth - 1), barY);
        API_END();
    }
    else if (style & controlStyleValueLeftRight_100_100 && value >= -100 && value <= 100)
    {

        uint16_t halfFill = barHeight / 2;

        uint16_t barFillYtop =
          (barY + 1) + (value > 0 ? ((halfFill * (100 - value)) / 100) : halfFill);
        uint16_t barFillYbott =
          (barY) + barHeight - (value < 0 ? ((halfFill * (100 + value)) / 100) : halfFill);

        API_COLOR(colors[0]);

        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(barX + 1, barFillYtop);
        API_VERTEX2F(barX + barWidth - 1, barFillYbott);
        API_END();
    }
    else if (style & controlStyleValueLeftRight_24_24 && value >= -24 && value <= 24)
    {
        uint16_t barFillY = barHeight - (barHeight * value) / 100;

        API_COLOR(colors[0]);

        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(barX + 1, barY + barFillY + 1);
        API_VERTEX2F(barX + barWidth - 1, barY + barHeight - 1);
        API_END();
    }
}
