#include "FT812.h"
#include "display.h"
#include "displayControls.h"
#include "songPlayerControl.h"
#include <algorithm>
#include <string.h>

#define TOTAL_BLOCKS_WIDTH 486
#define BLOCKS_MARGIN      5
#define BLOCK_SPACING      4
#define BLOCK_HEIGHT       23

#define MAX_BLOCKS_VISIBLE   32
#define MAX_PATTERNS_VISIBLE 12

constexpr uint16_t blockWidth =
  ((TOTAL_BLOCKS_WIDTH - BLOCKS_MARGIN * 2) - ((kAllTracks - 1) * BLOCK_SPACING)) / kAllTracks;
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
cSongPlayer::cSongPlayer(strControlProperties* properties)
  : cDisplayControl(controlIdSongPlayer, 3)
{
    colorsCount = 10;
    colors = display.getTheme().songPlayerColors;
    autoRefresh = false;

    refreshStep = 0;
    listAnimationStep = 0;
    disableBar = 0;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        text = nullptr;
        value = -1;
        width = 0;
        height = 0;
        style = 0;
        return;
    }

    posX = properties->x;
    posY = properties->y;

    text = properties->text;
    value = properties->value;

    width = properties->w;
    height = properties->h;

    // padNames = (strPadNames*)properties->data;

    setStyle(properties->style);
}

cSongPlayer::~cSongPlayer()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cSongPlayer::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    int8_t textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    font = &fonts[textFont];
}

void cSongPlayer::setText(const char* text)
{
    this->text = text;
}

void cSongPlayer::setValue(int value)
{
    if (value >= list->length)
    {
        if (list->length > 0)
        {
            listPosition = list->length - 1;
        }
        else
        {
            listPosition = 0;
        }

        disableBar = 0;
    }
    else if (value < 0)
    {
        listPosition = 0;
        disableBar = 1;
    }
    else
    {
        listPosition = value;
        disableBar = 0;
    }
}

void cSongPlayer::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cSongPlayer::setData(void* data)
{
    controlData = (player_data_t*)data;

    list = controlData->list;
    if (list->start < 0)
    {
        list->start = 0;
        listPosition = 0;
        disableBar = 1;
    }
    else
    {
        listPosition = list->start;
        disableBar = 0;
    }

    if (listPosition == 0) // pierwsza pozycja listy*
    {
        barPos = 0;
        textListPos = 0;
    }
    else if (listPosition == 1) // druga pozycja listy
    {
        barPos = 1;
        textListPos = 0;
    }
    else if (listPosition >= list->length - 1) // ostatnia pozycja listy
    {
        barPos = (list->length > list->linesCount ? list->linesCount - 1 : list->length - 1);
        textListPos = (list->length > list->linesCount ? list->length - (list->linesCount) : 0);
    }
    else if (listPosition >= list->length - 2) // przed ostatnia pozycja listy
    {
        barPos = (list->length > list->linesCount ? list->linesCount - 2 : list->length - 2);
        textListPos = (list->length > list->linesCount ? list->length - (list->linesCount) : 0);
    }
    else
    {
        barPos = (list->length > list->linesCount
                    ? (listPosition > list->linesCount - 2 ? list->linesCount - 2 : listPosition)
                    : listPosition);
        textListPos =
          (list->length > list->linesCount
             ? (listPosition > list->linesCount - 2 ? listPosition - (list->linesCount - 2) : 0)
             : 0);
    }
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cSongPlayer::update()
{
    // tlo listy
    if (style & controlStyleBackground)
    {
        API_COLOR(colors[3]);
        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(posX, this->posY);
        API_VERTEX2F(posX + width, this->posY + this->height);
        API_END();

        API_BLEND_FUNC(SRC_ALPHA, ZERO);

        if (style & controlStyleBottomShadow)
        {
            API_SAVE_CONTEXT();
            uint16_t grad_y = posY + height - 10;
            uint16_t grad_h = 12;
            API_SCISSOR_XY(posX, grad_y);
            API_SCISSOR_SIZE(width + 1, grad_h);
            API_CMD_GRADIENT(0, grad_y, colors[3], 0, grad_y + grad_h, 0x0);
            API_RESTORE_CONTEXT();
        }

        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
    }

    API_COLOR(colors[0]);
    API_CMD_TEXT(600, posY + 30, font->handle, textStyle | OPT_CENTERY | OPT_CENTERX, "Pattern");

    showList();
}

void cSongPlayer::update2()
{
    drawTrackNames();
}

void cSongPlayer::update3()
{
    if ((controlData->songData != nullptr) && (controlData->normalPatternsBitmask != nullptr) &&
        (controlData->midiPatternsBitmask != nullptr))
    {
        fillBlocks();
    }

    drawProgressLine();
}

void cSongPlayer::showList()
{
    if (list == nullptr)
    {
        return;
    }

    int16_t posY = this->posY + 62;
    int16_t width = 210;
    int16_t posX = (this->posX + this->width) - width;
    int16_t height = this->height - 30;

    int16_t x_pos = posX, y_pos, h_row = 27; // font->height+8;
    uint16_t w_bar = width - 6;              // szerokosc ramki
    uint16_t lines;

    if (list->length > list->linesCount)
    {
        w_bar = width - 13;
    }

    if (style & controlStyleHexNumbers)
    {
        API_CMD_SETBASE(16);
    }
    else
    {
        API_CMD_SETBASE(10);
    }

    if (list->start == listPosition)
    {
        // generowanie poczatkowego menu
        listAnimationStep = 0;
        autoRefresh = false;

        if (!disableBar)
        {
            x_pos = posX + 3;
            y_pos = posY + (barPos * h_row);

            // ramka
            if (controlData->isListActive == 0)
            {
                API_COLOR(colors[1]); // bialy - nie aktywny
            }
            else if (*controlData->isEditMode == 1)
            {
                API_COLOR(colors[5]); // one_true_red
            }
            else
            {
                API_COLOR(colors[9]);
            }

            API_LINE_WIDTH(kRedFrameWidth);
            API_BEGIN(LINE_STRIP);
            API_VERTEX2F(x_pos, y_pos);
            API_VERTEX2F(x_pos + w_bar, y_pos);
            API_VERTEX2F(x_pos + w_bar, y_pos + h_row);
            API_VERTEX2F(x_pos, y_pos + h_row);
            API_VERTEX2F(x_pos, y_pos);
            API_END();
        }

        // lista tekstow
        x_pos = posX + 7;
        y_pos = posY + h_row / 2;

        lines = std::max(list->length, textListPos) - textListPos;
        lines = std::min(lines, list->linesCount);

        API_SAVE_CONTEXT();
        API_SCISSOR_XY(posX, posY);
        API_SCISSOR_SIZE(w_bar, h_row * list->linesCount);

        for (uint8_t i = 0; i < lines; i++)
        {

            API_COLOR(colors[0]);

            // API_CMD_NUMBER(x_pos + 25,
            //                y_pos + (i * h_row),
            //                font->handle,
            //                textStyle | OPT_CENTERX | OPT_CENTERY,
            //                i + textListPos + 1);

            API_CMD_NUMBER(x_pos + 25,
                           y_pos + (i * h_row),
                           font->handle,
                           textStyle | OPT_CENTERY,
                           *(list->data + (i + textListPos)));

            if (list->names != nullptr)
            {

                API_CMD_TEXT(x_pos + 55,
                             y_pos + (i * h_row),
                             font->handle,
                             textStyle | OPT_CENTERY,
                             *(list->names + (i + textListPos)));
            }

            drawIcon(i + textListPos, i, x_pos, y_pos, h_row);
        }

        API_RESTORE_CONTEXT();
    }
    else
    {
        int8_t dir;         // kierunek przewijania: up = -1, down = 1
        int8_t mode;        // tryb przewijania: belka = 0, tekst = 1
        uint16_t diffrence; // skok belki/listy w pixelach przy jedym odsiwezeniu listy

        // Geneorwanie zmieny menu.

        // okreslenie kierunku i skoku listy
        if (list->start > listPosition)
        {
            dir = -1;
            diffrence = list->start - listPosition;
        }
        else
        {
            dir = 1;
            diffrence = listPosition - list->start;
        }

        if (diffrence > 8)
            diffrence = 75;
        else if (diffrence > 4)
            diffrence = 50;
        else if (diffrence > 1)
            diffrence = 25;
        else
            diffrence = 8;

        mode = 0;
        if (barPos == 0) // góra listy
        {
            mode = 0;
        }
        else if (barPos == 1) // druga pozycja
        {
            if (dir == 1 || list->start == 1)
                mode = 0;
            else
                mode = 1;
        }
        else if (barPos == list->linesCount - 2) // przedostatnia pozycja
        {
            if (dir == -1 || list->start == list->length - 2)
                mode = 0;
            else
                mode = 1;
        }
        else if (barPos == list->linesCount - 1) // ostatnia pozycja
        {
            if (dir == 1)
                mode = 1;
            else
                mode = 0;
        }

        if (mode == 0)
        {
            listAnimationStep = listAnimationStep + (dir * diffrence);
            if (listAnimationStep >= h_row)
            {
                listAnimationStep = 0;
                barPos = barPos + 1;
                list->start = list->start + 1;
            }
            else if (listAnimationStep <= -1 * h_row)
            {
                listAnimationStep = 0;
                barPos = barPos - 1;
                list->start = list->start - 1;
            }
        }
        else
        {
            listAnimationStep = listAnimationStep + (dir * diffrence);
            if (listAnimationStep >= h_row)
            {
                listAnimationStep = 0;
                list->start = list->start + 1;
                textListPos = textListPos + 1;
            }
            else if (listAnimationStep <= -1 * h_row)
            {
                listAnimationStep = 0;
                list->start = list->start - 1;

                if (textListPos)
                {
                    textListPos = textListPos - 1;
                }
            }
        }

        autoRefresh = true;

        if (!disableBar)
        {
            x_pos = posX + 3;
            y_pos = posY + (barPos * h_row) + (mode ? 0 : listAnimationStep);
            if (list->length > list->linesCount)
                w_bar = width - 13;

            // ramka
            if (controlData->isListActive == 0)
            {
                API_COLOR(colors[1]); // bialy - nie aktywny
            }
            else if (*controlData->isEditMode == 1)
            {
                API_COLOR(colors[5]); // one_true_red
            }
            else
            {
                API_COLOR(colors[9]);
            }

            API_LINE_WIDTH(kRedFrameWidth);
            API_BEGIN(LINE_STRIP);
            API_VERTEX2F(x_pos, y_pos);
            API_VERTEX2F(x_pos + w_bar, y_pos);
            API_VERTEX2F(x_pos + w_bar, y_pos + h_row);
            API_VERTEX2F(x_pos, y_pos + h_row);
            API_VERTEX2F(x_pos, y_pos);
            API_END();
        }

        x_pos = posX + 7;
        y_pos = posY + h_row / 2 + (mode ? ((-1 * h_row) - listAnimationStep) : 0);

        API_SAVE_CONTEXT();
        API_SCISSOR_XY(posX, posY);
        API_SCISSOR_SIZE(w_bar, h_row * list->linesCount);

        lines = std::max(list->length, textListPos) - textListPos;
        lines = std::min(lines, list->linesCount);

        if (mode == 1)
        {
            if (dir == 1) // tekst porusza sie w gore (wartosci w dol)
            {
                y_pos += h_row;

                for (int8_t i = 0; i < lines; i++)
                {
                    API_COLOR(colors[0]);

                    // API_CMD_NUMBER(x_pos + 25,
                    //                y_pos + (i * h_row),
                    //                font->handle,
                    //                textStyle | OPT_CENTERX | OPT_CENTERY,
                    //                i + textListPos + 1);

                    API_CMD_NUMBER(x_pos + 25,
                                   y_pos + (i * h_row),
                                   font->handle,
                                   textStyle | OPT_CENTERY,
                                   *(list->data + (i + textListPos)));

                    if (list->names != nullptr)
                    {

                        API_CMD_TEXT(x_pos + 55,
                                     y_pos + (i * h_row),
                                     font->handle,
                                     textStyle | OPT_CENTERY,
                                     *(list->names + (i + textListPos)));
                    }

                    drawIcon(i + textListPos, i, x_pos, y_pos, h_row);
                }
            }
            else if (dir == -1) // tekst porusza sie w dol (wartosci w gore)
            {
                for (int8_t i = 0; i < (lines + 1); i++)
                {

                    API_COLOR(colors[0]);

                    int16_t table_offset = textListPos - 1;
                    if (table_offset < 0)
                        table_offset = 0;

                    // API_CMD_NUMBER(x_pos + 25,
                    //                y_pos + (i * h_row),
                    //                font->handle,
                    //                textStyle | OPT_CENTERX | OPT_CENTERY,
                    //                i + textListPos + 1);

                    API_CMD_NUMBER(x_pos + 25,
                                   y_pos + (i * h_row),
                                   font->handle,
                                   textStyle | OPT_CENTERY,
                                   *(list->data + (i + table_offset)));

                    if (list->names != nullptr)
                    {

                        API_CMD_TEXT(x_pos + 55,
                                     y_pos + (i * h_row),
                                     font->handle,
                                     textStyle | OPT_CENTERY,
                                     *(list->names + (i + table_offset)));
                    }

                    drawIcon(i + table_offset, i, x_pos, y_pos, h_row);
                }
            }
        }
        else
        {
            for (uint8_t i = 0; i < lines; i++)
            {
                API_COLOR(colors[0]);

                // API_CMD_NUMBER(x_pos + 25,
                //                y_pos + (i * h_row),
                //                font->handle,
                //                textStyle | OPT_CENTERX | OPT_CENTERY,
                //                i + textListPos + 1);

                API_CMD_NUMBER(x_pos + 25,
                               y_pos + (i * h_row),
                               font->handle,
                               textStyle | OPT_CENTERY,
                               *(list->data + (i + textListPos)));

                if (list->names != nullptr)
                {

                    API_CMD_TEXT(x_pos + 55,
                                 y_pos + (i * h_row),
                                 font->handle,
                                 textStyle | OPT_CENTERY,
                                 *(list->names + (i + textListPos)));
                }

                drawIcon(i + textListPos, i, x_pos, y_pos, h_row);
            }
        }

        API_RESTORE_CONTEXT();
    }

    // pasek przewijania
    lines = std::max(list->length, textListPos) - textListPos;
    lines = std::min(lines, list->linesCount);
    if (list->length > list->linesCount || list->length > lines)
    {
        int16_t y_base = list->length + list->linesCount - lines;
        int16_t y_length = (height - 46) * std::min(list->linesCount, list->length) / y_base;
        y_pos = posY + 6 + (list->start * ((height - 46) - y_length)) / list->length;

        API_COLOR(colors[6]);
        API_LINE_WIDTH(kRedFrameWidth);
        API_BEGIN(RECTS);
        API_VERTEX2F(posX + (width - 6), posY + 4);
        API_VERTEX2F(posX + (width - 3), (posY + height) - 42);
        API_END();

        API_COLOR(colors[2]);
        API_LINE_WIDTH(kRedFrameWidth);
        API_BEGIN(LINES);
        API_VERTEX2F(posX + (width - 5), y_pos);
        API_VERTEX2F(posX + (width - 5), y_pos + y_length);

        API_VERTEX2F(posX + (width - 4), y_pos);
        API_VERTEX2F(posX + (width - 4), y_pos + y_length);

        API_END();
    }
}

void cSongPlayer::drawIcon(int pos, int row, int x, int y, int h)
{
    if (list->icon.useSpecialIcons)
    {
        if ((list->icon.iconPositionInList) == pos)
        {
            uint8_t idx = list->icon.iconNum;

            API_BITMAP_SOURCE(bitmaps[idx].address);
            API_BITMAP_LAYOUT(bitmaps[idx].format, bitmaps[idx].linestride, bitmaps[idx].height);
            API_BITMAP_SIZE(NEAREST, BORDER, BORDER, bitmaps[idx].width, bitmaps[idx].height);

            API_BEGIN(BITMAPS);
            API_VERTEX2F(x, y + ((row * h) - bitmaps[idx].height / 2 + 1));
            API_END();
        }
    }
}

void cSongPlayer::drawTrackNames()
{
    const int allTracksCount = controlData->normalTracksCount + controlData->midiTracksCount;

    for (int track = 0; track < allTracksCount; track++)
    {
        if (track < controlData->normalTracksCount
              ? controlData->normalTracksMute[track]
              : controlData->midiTracksMute[track - controlData->normalTracksCount])
        {
            API_COLOR(colors[4]);
        }
        else
        {
            API_COLOR(colors[0]);
        }

        if (style & controlStyleVerticalText)
        {
            strFont* nameFont = &fonts[4];

            API_SAVE_CONTEXT();
            API_BITMAP_HANDLE(nameFont->handle);
            API_BEGIN(BITMAPS);

            API_BITMAP_SIZE(NEAREST, BORDER, BORDER, nameFont->height, nameFont->height);

            API_BITMAP_ROTATE_270(nameFont->height);

            uint8_t strPtr = 0;
            int16_t x = posX + BLOCKS_MARGIN + ((blockWidth + BLOCK_SPACING) * track) +
                        (blockWidth / 2) - (nameFont->height / 2);
            int16_t y = posY + 42;
            char* text =
              (track < controlData->normalTracksCount)
                ? controlData->normalTracksNames + (track * controlData->normalTracksNamesBuffLen)
                : controlData->midiTracksNames + ((track - controlData->normalTracksCount) *
                                                  controlData->midiTracksNamesBuffLen);
            int16_t txtLen = strlen(text);

            API_BITMAP_HANDLE(nameFont->handle);
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
                    API_VERTEX2II(x, y, nameFont->handle, (char)text[strPtr]);
                }

                int character = (uint8_t)text[strPtr];
                if (character < 148)
                {
                    y -= nameFont->data[character] + 1;
                }
                strPtr++;
            }

            API_END();
            API_RESTORE_CONTEXT();
        }
        else
        {
#ifdef OLD_NAMES
            API_CMD_TEXT(
              posX + BLOCKS_MARGIN + (blockWidth * track) + (blockWidth / 2),
              posY + 18,
              fonts[4].handle,
              textStyle | OPT_CENTERY | OPT_CENTERX,
              (track < controlData->normalTracksCount)
                ? controlData->normalTracksNames + (track * controlData->normalTracksNamesBuffLen)
                : controlData->midiTracksNames + ((track - controlData->normalTracksCount) *
                                                  controlData->midiTracksNamesBuffLen),
              7);
#endif
        }
    }
}

void cSongPlayer::fillBlocks()
{
    API_LINE_WIDTH(kRedFrameWidth);
    API_BEGIN(RECTS);

    int maxPatternsVisible =
      std::max(controlData->songLength, (uint8_t)textListPos) - (uint8_t)textListPos;
    maxPatternsVisible = std::min(maxPatternsVisible, MAX_PATTERNS_VISIBLE);

    selectionCalcInit = 0;
    const int allTracksCount = controlData->normalTracksCount + controlData->midiTracksCount;

    for (int pattern = 0; pattern < maxPatternsVisible; pattern++)
    {
        uint16_t data =
          controlData->normalPatternsBitmask[controlData->songData[textListPos + pattern] - 1] |
          controlData->midiPatternsBitmask[controlData->songData[textListPos + pattern] - 1] << 8;
        uint16_t localY = posY + pattern * (BLOCK_HEIGHT + BLOCK_SPACING) + 64;

        for (int track = 0; track < allTracksCount; track++)
        {
            uint16_t localX = posX + BLOCKS_MARGIN + track * (blockWidth + BLOCK_SPACING);

            if (!(data & (1 << track)))
            {
                API_COLOR(colors[8]);
                API_VERTEX2F((localX), (localY));
                API_VERTEX2F((localX + blockWidth), (localY + BLOCK_HEIGHT));
            }
            else
            {
                if (track < controlData->normalTracksCount
                      ? controlData->normalTracksMute[track]
                      : controlData->midiTracksMute[track - controlData->normalTracksCount])
                {
                    API_COLOR(colors[7]);
                }
                else
                {
                    API_COLOR(colors[1]);
                }
                API_VERTEX2F((localX), (localY + 1));
                API_VERTEX2F((localX + blockWidth - 1), (localY + BLOCK_HEIGHT - 1));
            }

            calculateSelection(
              pattern, track, localX, localY, localX + blockWidth, localY + BLOCK_HEIGHT);
        }
    }

    API_END();

    drawSelection();
}

void cSongPlayer::drawProgressLine()
{
    if (controlData->progress.isPlaying)
    {
        API_BLEND_FUNC(SRC_ALPHA, ZERO);

        if (*controlData->isEditMode == 1)
        {
            API_COLOR(colors[5]);
        }
        else
        {
            API_COLOR(colors[9]);
        }

        API_LINE_WIDTH(kRedFrameWidth);
        API_BEGIN(LINES);

        int16_t patternPosition = controlData->progress.currentSongPosition - textListPos;

        if ((patternPosition < MAX_PATTERNS_VISIBLE) && (patternPosition >= 0))
        {
            float positionInPattern =
              controlData->progress.positionInPattern *
                (BLOCK_HEIGHT / (float)controlData->progress.patternLength) +
              0.5f; // 0.5 for rounding

            uint16_t localX, localY, localWidth;
            localX = posX + BLOCKS_MARGIN;
            localWidth = (blockWidth + BLOCK_SPACING) * kAllTracks - BLOCK_SPACING;

            if (controlData->progress.trackPreview < 8)
            {
                localX += controlData->progress.trackPreview * (blockWidth + BLOCK_SPACING);
                localWidth = blockWidth;
            }

            localY = posY + patternPosition * (BLOCK_HEIGHT + BLOCK_SPACING) + 64;

            API_VERTEX2F(localX, localY + positionInPattern);
            API_VERTEX2F(localX + localWidth, localY + positionInPattern);

            API_END();
        }

        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
    }
}

void cSongPlayer::calculateSelection(uint8_t pattern,
                                     uint8_t track,
                                     uint16_t x,
                                     uint16_t y,
                                     uint16_t x_end,
                                     uint16_t y_end)
{
    if (selectionCalcInit == 0)
    {
        xSelectionStart = UINT16_MAX;
        ySelectionStart = UINT16_MAX;
        xSelectionEnd = 0;
        ySelectionEnd = 0;

        selectionCalcInit = 1;
    }

    if (controlData->selection.isActive)
    {
        if ((textListPos + pattern) >= controlData->selection.startSongPosition)
        {
            uint8_t calculatedPattern = textListPos + pattern;
            uint8_t isSelected = 1;

            if (!(calculatedPattern >= controlData->selection.startSongPosition))
                isSelected = 0;
            if (!(calculatedPattern < (controlData->selection.startSongPosition +
                                       controlData->selection.patternSelectionLength)))
                isSelected = 0;
            if (!(track >= controlData->selection.startTrack))
                isSelected = 0;
            if (!(track < (controlData->selection.startTrack +
                           controlData->selection.trackSelectionLength)))
                isSelected = 0;

            if (isSelected)
            {
                if (x < xSelectionStart)
                {
                    xSelectionStart = x;
                }

                if (y < ySelectionStart)
                {
                    ySelectionStart = y;
                }

                if (x_end > xSelectionEnd)
                {
                    xSelectionEnd = x_end;
                }

                if (y_end > ySelectionEnd)
                {
                    ySelectionEnd = y_end;
                }
            }
        }
    }
}

void cSongPlayer::drawSelection()
{
    if (xSelectionStart != UINT16_MAX && ySelectionStart != UINT16_MAX && xSelectionEnd != 0 &&
        ySelectionEnd != 0)
    {
        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        API_COLOR(colors[5]);
        API_LINE_WIDTH(kRedFrameWidth);
        API_BEGIN(LINE_STRIP);

        API_VERTEX2F(xSelectionStart, ySelectionStart);
        API_VERTEX2F(xSelectionEnd, ySelectionStart);
        API_VERTEX2F(xSelectionEnd, ySelectionEnd);
        API_VERTEX2F(xSelectionStart, ySelectionEnd);
        API_VERTEX2F(xSelectionStart, ySelectionStart);

        API_END();
        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
    }
}
