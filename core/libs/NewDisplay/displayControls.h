
#ifndef LIBS_NEWDISPLAY_DISPLAYCONTROLS_H_
#define LIBS_NEWDISPLAY_DISPLAYCONTROLS_H_

#include "DisplayDriver.h"
#include "displayStructs.h"

//--------------------------------------------------------------------
// klasa wirtualna bazowa dla kontrolek
class cDisplayControl
{
    friend class cDisplay;
    friend class cDebugLog;

  public:
    virtual void update() = 0;
    virtual void update2(){};
    virtual void update3(){};
    virtual void update4(){};
    virtual void update5(){};

    virtual void setStyle(uint32_t style) = 0;
    virtual void setText(const char* text) = 0;
    virtual void setValue(int value) = 0;
    virtual void setColors(const uint32_t* colors) = 0;
    virtual void setData(void* data) = 0;

    cDisplayControl(control_id_t _id, uint8_t _stagesCount)
      : id(_id)
      , stagesCount(_stagesCount)
    {
        refresh = 0;
        ramSize = 0;
        autoRefresh = false;
        highPrioRefresh = false;
        style = 0;
        text = nullptr;
        font = nullptr;
        value = 0;
        posX = 0;
        posY = 0;
        width = 0;
        height = 0;
        colorsCount = 10;
        colors = nullptr;
    }
    virtual ~cDisplayControl() {}

    static void setDisplayDriver(DisplayDriver* displayDriver)
    {
        cDisplayControl::displayDriver = displayDriver;
    }

  protected:
    const control_id_t id;
    const uint8_t stagesCount;
    uint16_t ramSize;
    bool autoRefresh;
    bool highPrioRefresh;

    uint8_t refresh;
    // Index of the buffer in cDisplay that holds this control's display list.
    int8_t fragment = -1;

    uint32_t style;
    const char* text;
    const strFont* font;
    int value;

    uint16_t posX;
    uint16_t posY;

    uint16_t width;
    uint16_t height;

    uint8_t colorsCount;
    const uint32_t* colors;

    static DisplayDriver* displayDriver;

    void string2Bitmaps(int16_t x, int16_t y, const char* string, int8_t length, bool constWidth);

    bool isStyleSet(controlsStyle style) { return (this->style & style); }
};

#endif
