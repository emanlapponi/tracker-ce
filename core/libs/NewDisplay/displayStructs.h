
#ifndef LIBS_NEWDISPLAY_DISPLAYSTRUCTS_H_
#define LIBS_NEWDISPLAY_DISPLAYSTRUCTS_H_

#include <stddef.h>
#include <stdint.h>

class cDisplayControl;
class cDebugLog;
class cDisplay;

extern "C"
{
    extern void* memcpy(void* dst, const void* src, size_t count);
}

// #########################################################################
//							DEFINICJE
// #########################################################################

const uint8_t controlsCount = 65;

const uint32_t imgRamAddress = 670000;
const uint32_t imgSizeMax = 1000000 - imgRamAddress;
const int32_t imgBufforSize = 5000;

extern const uint8_t displayFontCount;

enum enDisplayBitmaps
{
    displayPolyLogoBitmap,
    displayPlayIcon,
    displayLoopIcon,

    displayDoubleArrowL,
    displayDoubleArrowR,
    displayArrowU,
    displayArrowD,

    displayBattery,
    displayBatteryCh,

#ifdef FEATURE_SYNTH
    kBitmapEnv,
    kBitmapFilter,
    kBitmapGear,
    kBitmapLfo,
    kBitmapMacro,
    kBitmapMod,
    kBitmapOsc,

    kBitmapFX,
    kBitmapCymbal,
    kBitmapHihat,
    kBitmapKick,
    kBitmapMixer,
    kBitmapPercussion,
    kBitmapSnare,
    kBitmapTom,

    kBitmapShaper,

#endif

    displayBitmapsCount,
};

enum controlsStyle
{
    controlStyleHide = 0,
    controlStyleShow = 1,

    controlStyleCreateSubcontrols = 2,

    controlStyleCenterX = 4,
    controlStyleCenterY = 8,
    controlStyleRightX = 16,

    controlStyleShowBitmap = (1 << 5),
    controlStyleUnderline = (1 << 6),
    controlStyleVerticalText = (1 << 7),

    controlStyleFont1 = 256,       // 14 Regular
    controlStyleFont2 = 512,       // 17 Regular
    controlStyleFont3 = 512 + 256, // 14 Bold
    controlStyleFont4 = 1024,      // 17 Bold

    controlStyleFont5 = 256 + 1024, // 12 Regular
    controlStyleFont6 = 512 + 1024,
    controlStyleFont7 = 512 + 256 + 1024,
    controlStyleFont8 = 1024 + 1024,

    controlStyleBorder = (1 << 12),
    controlStyleBackground = (1 << 13),
    controlStyleRoundedBorder = (1 << 14),
    controlStyleNoTransparency = (1 << 15),

    controlStyleValueLeftRight_100_100 = (1 << 16),
    controlStyleValueLeftRight_24_24 = (1 << 17),
    controlStyleValue_0_100 = (1 << 18),
    controlStyleValueNumberOnly = (1 << 19),
    controlStyleCompareTwoValues = (1 << 20),

    controlStyleBottomShadow = (1 << 21),

    controlStyleTransformImg = (1 << 22),

    controlStyleHexNumbers = (1 << 23),
    controlStyleStartNumbersFrom1 = (1 << 24),

    controlStyleReverse_0_100 = (1 << 25),

    controlStyleGradient = (1 << 26),

    controlStyleSpecialFx = (1 << 27),  // underlines
    controlStyleSpecialFx2 = (1 << 28), // dots
    controlStyleLimitTextWidth = (1 << 29),
};

typedef enum enControlId
{
    controlIdBar,
    controlIdBgLabel,
    controlIdFrame,
    controlIdEdit,
    controlIdImg,
    controlIdLabel,
    controlIdHorizontalBar,
    controlIdLineIndicator,
    controlIdList,
    controlIdKeyboard,
    controlIdNotePopup,
    controlIdMultiLabel,
    controlIdParamValueList,
    controlIdPoints,
    controlIdProcessingPopup,
    controlIdProgressCursor,
    controlIdSimpleText,
    controlIdSlicePoints,
    controlIdSongPlayer,
    controlIdSpectrum,
    controlIdStartScreen,
    controlIdTemplate,
    controlIdTextBox,
    controlIdTextPopup,
    controlIdTracker,
    controlIdWtProgress,
    controlIdArrangement,
    ControlIdBattery,
    controlIdBoxMenu,
    controlIdImgList,
    controlIdImgGrid,
    controlIdSimpleBox,
    controlIdNotesKeyboardLabel,

    /*
     *
    : cDisplayControl(controlId)

    */
} control_id_t;

struct strControlProperties
{
    const char* text = nullptr;
    int value = 0;
    void* data = nullptr;

    uint32_t style = 0;
    const uint32_t* colors = nullptr;

    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t w = 0;
    uint16_t h = 0;
};

struct strFont
{
    // The start of the font's metric block. Only 148 bytes are metrics; see
    // displayList::kFontMetricsSize for why more are kept.
    uint8_t data[256];
    uint32_t size;
    int16_t handle;
    uint32_t address;
    int32_t source;
    uint16_t width;
    uint16_t height;
    uint8_t format;
    uint16_t linestride;
};

struct strBitmap
{
    const uint8_t* data;
    uint32_t address;
    uint32_t source;
    uint16_t width;
    uint16_t height;
    uint8_t format;
    uint16_t linestride;
};

struct strGetProps
{
    uint32_t End_Address;
    uint32_t Width;
    uint32_t Height;
};

extern strFont fonts[];
extern const strBitmap bitmaps[];

extern const strBitmap bitmaps[];
extern strGetProps getProps;

#define FONT_INDEX_FROM_STYLE (((style >> 8) & 15) - 1)

uint16_t getTextWidth(const strFont* font, const char* text, uint16_t length);
uint16_t getMaxTextLengthThatFitSpecificWidth(const strFont* font,
                                              const char* text,
                                              uint16_t widthLimit);

// NIE-EDITABLE
constexpr uint8_t controlsRefreshQueueSize = controlsCount + 1;

#define DISP_RGB(red, green, blue)                                                                 \
    ((((red) & 255UL) << 16) | (((green) & 255UL) << 8) | (((blue) & 255UL) << 0))

uint8_t get_jpeg_size(uint8_t* data, int32_t data_size, uint16_t* width, uint16_t* height);

typedef int8_t hControl; // cDisplayControl*

#endif /* LIBS_NEWDISPLAY_DISPLAYSTRUCTS_H_ */
