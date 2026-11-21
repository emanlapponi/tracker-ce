#include <stdio.h>
#include <string.h>

#include "ThemeManager.h"
#include "SdTextFile.h"
#include "Platform.h"
#include "core/interface.h"
#include "display.h"

#include "cr_section_macros.h"

Platform* ThemeManager::platform = nullptr;

uint8_t ThemeManager::themeCount = 3;
__NOINIT(EXTERNAL_RAM) char ThemeManager::themeNames[ThemeManager::kMaxCustomThemes][ThemeManager::kMaxNameLength];
__NOINIT(EXTERNAL_RAM) const char* ThemeManager::themeNamePointers[ThemeManager::kMaxCustomThemes];
__NOINIT(EXTERNAL_RAM) char ThemeManager::themeFilePaths[ThemeManager::kMaxCustomThemes][64];
uint8_t ThemeManager::currentThemeIndex = 0;

uint8_t ThemeManager::fontCount = 2;
__NOINIT(EXTERNAL_RAM) char ThemeManager::fontNames[ThemeManager::kMaxCustomFonts][ThemeManager::kMaxNameLength];
__NOINIT(EXTERNAL_RAM) const char* ThemeManager::fontNamePointers[ThemeManager::kMaxCustomFonts];
__NOINIT(EXTERNAL_RAM) char ThemeManager::fontFilePaths[ThemeManager::kMaxCustomFonts][64];
uint8_t ThemeManager::currentFontIndex = 0;

__NOINIT(EXTERNAL_RAM) static char s_themeNamesBuffer[ThemeManager::kMaxCustomThemes * ThemeManager::kMaxNameLength];
__NOINIT(EXTERNAL_RAM) static char* s_themeFileList[ThemeManager::kMaxCustomThemes];

__NOINIT(EXTERNAL_RAM) static char s_fontNamesBuffer[ThemeManager::kMaxCustomFonts * ThemeManager::kMaxNameLength];
__NOINIT(EXTERNAL_RAM) static char* s_fontFileList[ThemeManager::kMaxCustomFonts];

__NOINIT(EXTERNAL_RAM) static char s_fileReadBuffer[8192];

void ThemeManager::ensureDefaultsInitialized()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    strcpy(themeNames[0], "Original");
    strcpy(themeNames[1], "Monochrome");
    strcpy(themeNames[2], "Anniversary");
    themeNamePointers[0] = themeNames[0];
    themeNamePointers[1] = themeNames[1];
    themeNamePointers[2] = themeNames[2];

    strcpy(fontNames[0], "Original");
    strcpy(fontNames[1], "New");
    fontNamePointers[0] = fontNames[0];
    fontNamePointers[1] = fontNames[1];
}

static bool hasExtension(const char* filename, const char* ext)
{
    if (!filename || !ext)
        return false;
    size_t flen = strlen(filename);
    size_t elen = strlen(ext);
    if (flen < elen)
        return false;
    return strcasecmp(filename + flen - elen, ext) == 0;
}

static inline int hexVal(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool ThemeManager::parseColor(const char* str, size_t len, uint32_t& outColor)
{
    if (!str || len != 7 || str[0] != '#')
        return false;

    int r1 = hexVal(str[1]), r2 = hexVal(str[2]);
    int g1 = hexVal(str[3]), g2 = hexVal(str[4]);
    int b1 = hexVal(str[5]), b2 = hexVal(str[6]);
    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0)
        return false;

    outColor = DISP_RGB((r1 << 4) | r2, (g1 << 4) | g2, (b1 << 4) | b2);
    return true;
}

struct ColorKeyMapping
{
    const char* key;
    uint8_t index;
};

static constexpr ColorKeyMapping kQuadMap[] = {
    { "text", 0 },
    { "background", 1 },
    { "frame", 2 },
    { "text2", 3 },
};

static constexpr ColorKeyMapping kTripleMap[] = {
    { "text", 0 },
    { "background", 1 },
    { "frame", 2 },
};

static constexpr ColorKeyMapping kWaveformPairMap[] = {
    { "text", 0 },
    { "audio", 0 },
    { "active", 0 },
    { "position", 0 },
    { "cursor", 1 },
    { "background", 1 },
    { "inactive", 1 },
    { "progress", 1 },
};

static constexpr ColorKeyMapping kNotePopupMap[] = {
    { "padBackground", 0 },
    { "padText", 1 },
    { "selectionFrame", 2 },
};

static constexpr ColorKeyMapping kNoteKeyboardMap[] = {
    { "text", 0 },
    { "background", 1 },
    { "activeText", 2 },
};

static constexpr ColorKeyMapping kBatteryMap[] = {
    { "body", 0 },
    { "level", 1 },
    { "warning", 2 },
};

static constexpr ColorKeyMapping kListMap[] = {
    { "frameNormal", 0 },
    { "frameSelect", 1 },
    { "scrollbar", 2 },
    { "background", 3 },
    { "textNormal", 4 },
    { "textSelect", 5 },
    { "scrollbarTrack", 6 },
};

static constexpr ColorKeyMapping kMiniMenuMap[] = {
    { "text", 0 },
    { "boxBackground", 1 },
    { "selectedBox", 2 },
    { "background", 3 },
    { "frame", 4 },
    { "inactiveDot", 5 },
    { "activeDot", 6 },
};

static constexpr ColorKeyMapping kBarMap[] = {
    { "primary", 0 },
    { "accent1", 1 },
    { "accent", 1 },
    { "accent2", 2 },
    { "container", 3 },
    { "frame", 3 },
    { "background", 4 },
};

static constexpr ColorKeyMapping kPatternTrackerMap[] = {
    { "trackName", 0 },
    { "background", 1 },
    { "note", 2 },
    { "instrument", 3 },
    { "fx1", 4 },
    { "fx2", 5 },
    { "inactiveNote", 6 },
    { "inactiveInstrument", 7 },
    { "inactiveFx1", 8 },
    { "inactiveFx2", 9 },
    { "selection", 10 },
    { "gridDivider", 11 },
    { "inactiveTrack", 12 },
    { "playhead", 13 },
    { "selectionText", 14 },
    { "verticalDivider", 15 },
    { "stepNumber", 16 },
    { "greenMode", 17 },
    { "unfocusedTrack", 18 },
    { "activeTrack", 19 },
    { "trackEnd", 20 },
};

static constexpr ColorKeyMapping kSongPlayerMap[] = {
    { "text", 0 },
    { "activeBlock", 1 },
    { "scrollbar", 2 },
    { "background", 3 },
    { "inactiveText", 4 },
    { "editMode", 5 },
    { "scrollbarBackground", 6 },
    { "playhead", 7 },
    { "mutedBlock", 7 },
    { "emptyTrack", 8 },
    { "playheadActive", 9 },
};

static constexpr ColorKeyMapping kMoreButtonMap[] = {
    { "text", 0 },
    { "background", 1 },
    { "activeDot", 2 },
    { "inactiveDot", 3 },
};

static constexpr ColorKeyMapping kEditBoxMap[] = {
    { "text", 0 },
    { "boxBackground", 1 },
    { "frame", 2 },
    { "selection", 3 },
    { "outerBackground", 4 },
};

static constexpr ColorKeyMapping kKeyboardMap[] = {
    { "keyBackground", 0 },
    { "text", 1 },
    { "activeKey", 2 },
};

// Minimal, zero-allocation streaming JSON reader
struct JsonReader
{
    const char* json;
    size_t len;
    size_t pos;

    void skipWhitespace()
    {
        while (pos < len && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n'))
            pos++;
    }

    bool match(char c)
    {
        skipWhitespace();
        if (pos < len && json[pos] == c)
        {
            pos++;
            return true;
        }
        return false;
    }

    bool peek(char c)
    {
        skipWhitespace();
        return (pos < len && json[pos] == c);
    }

    bool readString(char* outBuf, size_t maxLen)
    {
        skipWhitespace();
        if (pos >= len || json[pos] != '"')
            return false;
        pos++;
        size_t idx = 0;
        while (pos < len && json[pos] != '"')
        {
            if (json[pos] == '\\' && pos + 1 < len)
            {
                pos++;
            }
            if (idx + 1 < maxLen)
            {
                outBuf[idx++] = json[pos];
            }
            pos++;
        }
        if (pos < len && json[pos] == '"')
            pos++;
        outBuf[idx] = '\0';
        return true;
    }

    void skipValue()
    {
        skipWhitespace();
        if (pos >= len)
            return;
        char c = json[pos];
        if (c == '"')
        {
            char dummy[16];
            readString(dummy, sizeof(dummy));
        }
        else if (c == '{' || c == '[')
        {
            char openChar = c;
            char closeChar = (c == '{') ? '}' : ']';
            pos++;
            int depth = 1;
            while (pos < len && depth > 0)
            {
                if (json[pos] == openChar)
                    depth++;
                else if (json[pos] == closeChar)
                    depth--;
                else if (json[pos] == '"')
                {
                    char dummy[16];
                    readString(dummy, sizeof(dummy));
                    continue;
                }
                pos++;
            }
        }
        else
        {
            while (pos < len && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
                   json[pos] != ' ' && json[pos] != '\r' && json[pos] != '\n' && json[pos] != '\t')
            {
                pos++;
            }
        }
    }

    bool readColor(uint32_t& outColor)
    {
        char str[16];
        if (readString(str, sizeof(str)))
        {
            return ThemeManager::parseColor(str, strlen(str), outColor);
        }
        skipValue();
        return false;
    }

    bool readColorMap(uint32_t* out, size_t maxCount, const ColorKeyMapping* map, size_t mapLen, bool allowScalar = false)
    {
        skipWhitespace();
        if (match('{'))
        {
            while (!peek('}') && pos < len)
            {
                char key[32];
                if (!readString(key, sizeof(key)))
                    break;
                match(':');
                bool matched = false;
                for (size_t i = 0; i < mapLen; i++)
                {
                    if (strcasecmp(key, map[i].key) == 0)
                    {
                        readColor(out[map[i].index]);
                        matched = true;
                        break;
                    }
                }
                if (!matched)
                    skipValue();
                match(',');
            }
            match('}');
            return true;
        }
        else if (match('['))
        {
            for (size_t i = 0; i < maxCount && !peek(']'); i++)
            {
                readColor(out[i]);
                match(',');
            }
            match(']');
            return true;
        }
        else if (allowScalar)
        {
            return readColor(out[0]);
        }
        return false;
    }

    template <size_t N>
    bool readMapped(uint32_t* out, size_t maxCount, const ColorKeyMapping (&map)[N], bool allowScalar = false)
    {
        return readColorMap(out, maxCount, map, N, allowScalar);
    }

    bool readListColors(uint32_t* out)
    {
        skipWhitespace();
        if (match('{'))
        {
            bool hasFrameSelect = false;
            while (!peek('}') && pos < len)
            {
                char key[32];
                if (!readString(key, sizeof(key)))
                    break;
                match(':');
                bool matched = false;
                for (size_t i = 0; i < sizeof(kListMap) / sizeof(kListMap[0]); i++)
                {
                    if (strcasecmp(key, kListMap[i].key) == 0)
                    {
                        if (readColor(out[kListMap[i].index]) && kListMap[i].index == 1)
                            hasFrameSelect = true;
                        matched = true;
                        break;
                    }
                }
                if (!matched)
                    skipValue();
                match(',');
            }
            match('}');
            if (hasFrameSelect)
                out[0] = out[1];
            return true;
        }
        return readMapped(out, 7, kListMap);
    }

    bool readPerformance(Theme& outTheme)
    {
        skipWhitespace();
        if (match('{'))
        {
            while (!peek('}') && pos < len)
            {
                char key[32];
                if (!readString(key, sizeof(key)))
                    break;
                match(':');
                if (strcasecmp(key, "fxName") == 0)
                    readMapped(outTheme.perfFxNameTextLabelColors, 4, kQuadMap);
                else if (strcasecmp(key, "valueNormal") == 0)
                    readMapped(outTheme.perfValueNormalMLabelColors, 4, kQuadMap);
                else if (strcasecmp(key, "valueEdit") == 0)
                    readMapped(outTheme.perfValueEditMLabelColors, 4, kQuadMap);
                else if (strcasecmp(key, "background") == 0)
                {
                    uint32_t col = 0;
                    if (readColor(col))
                    {
                        outTheme.perfValueNormalMLabelColors[1] = col;
                        outTheme.perfValueEditMLabelColors[1] = col;
                        outTheme.perfFxNameTextLabelColors[1] = col;
                    }
                }
                else if (strcasecmp(key, "text") == 0)
                {
                    uint32_t col = 0;
                    if (readColor(col))
                    {
                        outTheme.perfValueNormalMLabelColors[0] = col;
                        outTheme.perfValueEditMLabelColors[0] = col;
                        outTheme.perfFxNameTextLabelColors[0] = col;
                    }
                }
                else
                    skipValue();
                match(',');
            }
            match('}');
            return true;
        }
        return false;
    }
};

bool ThemeManager::parseThemeJson(const char* jsonText, size_t jsonLen, Theme& outTheme)
{
    if (!jsonText || jsonLen == 0)
        return false;

    // Pre-scan for "base" template
    outTheme = themeDefault;
    {
        JsonReader baseScanner = { jsonText, jsonLen, 0 };
        if (baseScanner.match('{'))
        {
            while (!baseScanner.peek('}') && baseScanner.pos < baseScanner.len)
            {
                char k[32];
                if (!baseScanner.readString(k, sizeof(k)))
                    break;
                baseScanner.match(':');
                if (strcasecmp(k, "base") == 0)
                {
                    char baseVal[32];
                    if (baseScanner.readString(baseVal, sizeof(baseVal)))
                    {
                        if (strcasecmp(baseVal, "Monochrome") == 0)
                            outTheme = themeWhiteout;
                        else if (strcasecmp(baseVal, "Anniversary") == 0)
                            outTheme = themeAnniversary;
                    }
                    break;
                }
                baseScanner.skipValue();
                baseScanner.match(',');
            }
        }
    }

    bool customBgSet = false;
    bool customBlankSpaceSet = false;
    bool customStepBgSet = false;
    bool customStepDescSet = false;
    bool customMoreButtonSet = false;
    bool customEditBoxSet = false;
    bool customKeyboardSet = false;
    bool customDialogSet = false;
    bool customMiniMenuSet = false;
    bool customInactiveBarSet = false;
    bool customListBgSet = false;
    uint32_t customListBg = 0;
    bool customBarBgSet = false;
    uint32_t customBarBg = 0;
    bool customBarContainerSet = false;
    uint32_t customBarContainer = 0;

    bool customActiveLabelsSet = false;
    bool customInactiveLabelsSet = false;
    bool customActiveButtonSet = false;
    bool customInactiveButtonSet = false;
    bool customBottomButtonTextSet = false;
    bool customBottomButtonBgSet = false;
    uint32_t bottomButtonText = outTheme.activeButtonLabelsColors[0];
    bool customDefaultListSet = false;
    bool customActiveListSet = false;
    bool customInactiveListSet = false;

    uint32_t menuActiveColors[4];
    uint32_t menuInactiveColors[4];
    memcpy(menuActiveColors, outTheme.activeLabelsColors, sizeof(menuActiveColors));
    memcpy(menuInactiveColors, outTheme.inactiveLabelsColors, sizeof(menuInactiveColors));
    menuActiveColors[2] = outTheme.activeListColors[1];
    menuInactiveColors[2] = outTheme.inactiveListColors[0];

    JsonReader r = { jsonText, jsonLen, 0 };
    if (!r.match('{'))
        return false;

    while (!r.peek('}') && r.pos < r.len)
    {
        char key[32];
        if (!r.readString(key, sizeof(key)))
            break;
        r.match(':');

        if (strcasecmp(key, "background") == 0)
        {
            if (r.readColor(outTheme.backgroundColor))
                customBgSet = true;
        }
        else if (strcasecmp(key, "topBar") == 0)
        {
            r.readMapped(outTheme.topBarLabelColors, 4, kQuadMap);
        }
        else if (strcasecmp(key, "songTimer") == 0)
        {
            r.readMapped(outTheme.songTimerLabelColors, 4, kQuadMap);
        }
        else if (strcasecmp(key, "labels") == 0)
        {
            if (r.match('{'))
            {
                while (!r.peek('}') && r.pos < r.len)
                {
                    char lKey[32];
                    if (!r.readString(lKey, sizeof(lKey)))
                        break;
                    r.match(':');
                    if (strcasecmp(lKey, "active") == 0)
                    {
                        if (r.readMapped(menuActiveColors, 4, kQuadMap))
                        {
                            memcpy(outTheme.activeLabelsColors, menuActiveColors, sizeof(menuActiveColors));
                            customActiveLabelsSet = true;
                        }
                    }
                    else if (strcasecmp(lKey, "inactive") == 0)
                    {
                        if (r.readMapped(menuInactiveColors, 4, kQuadMap))
                        {
                            memcpy(outTheme.inactiveLabelsColors, menuInactiveColors, sizeof(menuInactiveColors));
                            customInactiveLabelsSet = true;
                        }
                    }
                    else if (strcasecmp(lKey, "activeButton") == 0)
                    {
                        if (r.readMapped(outTheme.activeButtonLabelsColors, 4, kQuadMap))
                            customActiveButtonSet = true;
                    }
                    else if (strcasecmp(lKey, "inactiveButton") == 0)
                    {
                        if (r.readMapped(outTheme.inactiveButtonLabelsColors, 4, kQuadMap))
                            customInactiveButtonSet = true;
                    }
                    else if (strcasecmp(lKey, "highlightedButton") == 0)
                        r.readMapped(outTheme.highlightedButtonLabelsColors, 4, kQuadMap);
                    else if (strcasecmp(lKey, "redButton") == 0)
                        r.readMapped(outTheme.redButtonLabelsColors, 4, kQuadMap);
                    else if (strcasecmp(lKey, "masterEditTrackName") == 0)
                        r.readMapped(outTheme.masterEditTrackNameLabelsColors, 4, kQuadMap);
                    else if (strcasecmp(lKey, "performActiveTrack") == 0)
                        r.readMapped(outTheme.performActiveTrackLabelsColors, 4, kQuadMap);
                    else if (strcasecmp(lKey, "performMutedTrack") == 0)
                        r.readMapped(outTheme.performMutedTrackLabelsColors, 4, kQuadMap);
                    else if (strcasecmp(lKey, "moreButton") == 0)
                    {
                        r.readMapped(outTheme.moreButtonColors, 4, kMoreButtonMap);
                        customMoreButtonSet = true;
                    }
                    else if (strcasecmp(lKey, "defaultText") == 0)
                        r.readMapped(outTheme.defaultTextColors, 4, kQuadMap);
                    else
                        r.skipValue();
                    r.match(',');
                }
                r.match('}');
            }
            else
            {
                r.skipValue();
            }
        }
        else if (strcasecmp(key, "bottomButtons") == 0)
        {
            r.skipWhitespace();
            if (r.match('{'))
            {
                while (!r.peek('}') && r.pos < r.len)
                {
                    char bKey[32];
                    if (!r.readString(bKey, sizeof(bKey)))
                        break;
                    r.match(':');
                    if (strcasecmp(bKey, "text") == 0)
                    {
                        if (r.readColor(bottomButtonText))
                            customBottomButtonTextSet = true;
                    }
                    else if (strcasecmp(bKey, "background") == 0)
                    {
                        if (r.readColor(outTheme.bottomButtonsBgColors[1]))
                            customBottomButtonBgSet = true;
                    }
                    else if (strcasecmp(bKey, "frame") == 0 || strcasecmp(bKey, "lines") == 0)
                    {
                        uint32_t col = 0;
                        if (r.readColor(col))
                        {
                            outTheme.bottomButtonsBgColors[0] = col;
                            outTheme.bottomButtonsBgColors[2] = col;
                        }
                    }
                    else
                    {
                        r.skipValue();
                    }
                    r.match(',');
                }
                r.match('}');
            }
            else
            {
                r.readMapped(outTheme.bottomButtonsBgColors, 3, kTripleMap);
            }
        }
        else if (strcasecmp(key, "patternTracker") == 0)
        {
            r.readMapped(outTheme.patternTrackerColors, 21, kPatternTrackerMap);
        }
        else if (strcasecmp(key, "songPlayer") == 0)
        {
            r.readMapped(outTheme.songPlayerColors, 10, kSongPlayerMap);
        }
        else if (strcasecmp(key, "frames") == 0)
        {
            if (r.match('{'))
            {
                while (!r.peek('}') && r.pos < r.len)
                {
                    char fKey[32];
                    if (!r.readString(fKey, sizeof(fKey)))
                        break;
                    r.match(':');
                    if (strcasecmp(fKey, "active") == 0)
                    {
                        r.readColor(outTheme.frameColors[0]);
                    }
                    else if (strcasecmp(fKey, "inactive") == 0)
                    {
                        uint32_t col = 0;
                        if (r.readColor(col))
                        {
                            outTheme.frameColors[1] = col;
                            outTheme.greenFrameColors[1] = col;
                        }
                    }
                    else if (strcasecmp(fKey, "green") == 0)
                    {
                        r.readColor(outTheme.greenFrameColors[0]);
                    }
                    else
                    {
                        r.skipValue();
                    }
                    r.match(',');
                }
                r.match('}');
            }
            else
            {
                r.skipValue();
            }
        }
        else if (strcasecmp(key, "lists") == 0)
        {
            if (r.match('{'))
            {
                while (!r.peek('}') && r.pos < r.len)
                {
                    char lKey[32];
                    if (!r.readString(lKey, sizeof(lKey)))
                        break;
                    r.match(':');
                    if (strcasecmp(lKey, "background") == 0)
                    {
                        if (r.readColor(customListBg))
                            customListBgSet = true;
                    }
                    else if (strcasecmp(lKey, "default") == 0)
                    {
                        uint32_t tmpList[7];
                        memcpy(tmpList, outTheme.activeListColors, sizeof(tmpList));
                        if (r.readListColors(tmpList))
                        {
                            memcpy(outTheme.activeListColors, tmpList, sizeof(tmpList));
                            memcpy(outTheme.inactiveListColors, tmpList, sizeof(tmpList));
                            memcpy(outTheme.projectActiveListColors, tmpList, sizeof(tmpList));
                            memcpy(outTheme.projectInactiveListColors, tmpList, sizeof(tmpList));
                            memcpy(outTheme.sampleLoaderActiveListColors, tmpList, sizeof(tmpList));
                            memcpy(outTheme.sampleLoaderInactiveListColors, tmpList, sizeof(tmpList));
                            memcpy(outTheme.stepPopupListColor, tmpList, sizeof(tmpList));
                            customDefaultListSet = true;
                        }
                    }
                    else if (strcasecmp(lKey, "active") == 0)
                    {
                        if (r.readListColors(outTheme.activeListColors))
                            customActiveListSet = true;
                    }
                    else if (strcasecmp(lKey, "inactive") == 0)
                    {
                        if (r.readListColors(outTheme.inactiveListColors))
                            customInactiveListSet = true;
                    }
                    else if (strcasecmp(lKey, "projectActive") == 0)
                        r.readListColors(outTheme.projectActiveListColors);
                    else if (strcasecmp(lKey, "projectInactive") == 0)
                        r.readListColors(outTheme.projectInactiveListColors);
                    else if (strcasecmp(lKey, "sampleLoader") == 0 || strcasecmp(lKey, "sampleLoaderActive") == 0)
                        r.readListColors(outTheme.sampleLoaderActiveListColors);
                    else if (strcasecmp(lKey, "sampleLoaderInactive") == 0)
                        r.readListColors(outTheme.sampleLoaderInactiveListColors);
                    else
                        r.skipValue();
                    r.match(',');
                }
                r.match('}');
            }
            else
            {
                r.skipValue();
            }
        }
        else if (strcasecmp(key, "bars") == 0)
        {
            if (r.match('{'))
            {
                while (!r.peek('}') && r.pos < r.len)
                {
                    char bKey[32];
                    if (!r.readString(bKey, sizeof(bKey)))
                        break;
                    r.match(':');
                    if (strcasecmp(bKey, "background") == 0)
                    {
                        if (r.readColor(customBarBg))
                            customBarBgSet = true;
                    }
                    else if (strcasecmp(bKey, "container") == 0)
                    {
                        if (r.readColor(customBarContainer))
                            customBarContainerSet = true;
                    }
                    else if (strcasecmp(bKey, "active") == 0)
                        r.readMapped(outTheme.activeBarColors, 5, kBarMap, true);
                    else if (strcasecmp(bKey, "inactive") == 0)
                    {
                        r.readMapped(outTheme.inactiveBarColors, 5, kBarMap, true);
                        customInactiveBarSet = true;
                    }
                    else if (strcasecmp(bKey, "sampleLoaderNormal") == 0)
                        r.readMapped(outTheme.sampleLoadMemBarNormalColors, 5, kBarMap, true);
                    else if (strcasecmp(bKey, "sampleLoaderFull") == 0)
                        r.readMapped(outTheme.sampleLoadMemBarFullColors, 5, kBarMap, true);
                    else
                        r.skipValue();
                    r.match(',');
                }
                r.match('}');
            }
            else
            {
                r.skipValue();
            }
        }
        else if (strcasecmp(key, "popups") == 0)
        {
            if (r.match('{'))
            {
                while (!r.peek('}') && r.pos < r.len)
                {
                    char pKey[32];
                    if (!r.readString(pKey, sizeof(pKey)))
                        break;
                    r.match(':');
                    if (strcasecmp(pKey, "stepTitle") == 0)
                        r.readMapped(outTheme.stepPopupTitleLabelColor, 4, kQuadMap);
                    else if (strcasecmp(pKey, "stepBackground") == 0)
                    {
                        r.readMapped(outTheme.stepPopupBgLabelColor, 4, kQuadMap);
                        customStepBgSet = true;
                    }
                    else if (strcasecmp(pKey, "stepDescription") == 0)
                    {
                        r.readMapped(outTheme.stepPopupDescriptionTextBoxColors, 3, kTripleMap);
                        customStepDescSet = true;
                    }
                    else if (strcasecmp(pKey, "stepList") == 0)
                        r.readListColors(outTheme.stepPopupListColor);
                    else if (strcasecmp(pKey, "note") == 0)
                        r.readMapped(outTheme.notePopupColors, 3, kNotePopupMap);
                    else if (strcasecmp(pKey, "noteKeyboard") == 0)
                        r.readMapped(outTheme.noteKeyboardColors, 3, kNoteKeyboardMap);
                    else if (strcasecmp(pKey, "dialog") == 0)
                    {
                        r.readMapped(outTheme.popupTextColors, 3, kTripleMap);
                        customDialogSet = true;
                    }
                    else if (strcasecmp(pKey, "progressPopup") == 0)
                        r.readMapped(outTheme.popupWithProgressColors, 3, kTripleMap);
                    else if (strcasecmp(pKey, "horizontalBar") == 0)
                        r.readMapped(outTheme.popupHorizontalBarColors, 4, kQuadMap);
                    else if (strcasecmp(pKey, "keyboard") == 0)
                    {
                        r.readMapped(outTheme.keyboardColors, 3, kKeyboardMap);
                        customKeyboardSet = true;
                    }
                    else if (strcasecmp(pKey, "editBox") == 0)
                    {
                        r.readMapped(outTheme.editBoxColors, 5, kEditBoxMap);
                        customEditBoxSet = true;
                    }
                    else if (strcasecmp(pKey, "configConfirm") == 0)
                        r.readMapped(outTheme.configConfirmPopUpLabelColors, 4, kQuadMap);
                    else if (strcasecmp(pKey, "miniMenu") == 0)
                    {
                        r.readMapped(outTheme.miniBoxMenuColors, 7, kMiniMenuMap);
                        customMiniMenuSet = true;
                    }
                    else
                        r.skipValue();
                    r.match(',');
                }
                r.match('}');
            }
            else
            {
                r.skipValue();
            }
        }
        else if (strcasecmp(key, "blankSpace") == 0)
        {
            if (r.peek('{') || r.peek('['))
                r.readMapped(outTheme.blankSpaceCoverLabelColors, 4, kQuadMap);
            else
            {
                uint32_t col = 0;
                if (r.readColor(col))
                    outTheme.blankSpaceCoverLabelColors[1] = col;
            }
            customBlankSpaceSet = true;
        }
        else if (strcasecmp(key, "battery") == 0)
        {
            r.readMapped(outTheme.batteryIconColors, 3, kBatteryMap);
        }
        else if (strcasecmp(key, "waveform") == 0)
        {
            if (r.match('{'))
            {
                while (!r.peek('}') && r.pos < r.len)
                {
                    char wKey[32];
                    if (!r.readString(wKey, sizeof(wKey)))
                        break;
                    r.match(':');
                    if (strcasecmp(wKey, "audio") == 0)
                        r.readColor(outTheme.waveformColors[0]);
                    else if (strcasecmp(wKey, "background") == 0)
                        r.readColor(outTheme.waveformColors[1]);
                    else if (strcasecmp(wKey, "points") == 0)
                        r.readMapped(outTheme.waveformPointsColors, 4, kQuadMap);
                    else if (strcasecmp(wKey, "progress") == 0)
                        r.readMapped(outTheme.waveformProgressColors, 2, kWaveformPairMap, true);
                    else if (strcasecmp(wKey, "granular") == 0)
                        r.readMapped(outTheme.granularProgressColors, 2, kWaveformPairMap, true);
                    else if (strcasecmp(wKey, "wavetablePosition") == 0)
                        r.readMapped(outTheme.waveTablePositionColors, 2, kWaveformPairMap, true);
                    else if (strcasecmp(wKey, "sliceMarker") == 0)
                        r.readColor(outTheme.sliceColors[0]);
                    else if (strcasecmp(wKey, "activeSlice") == 0)
                        r.readColor(outTheme.sliceColors[1]);
                    else if (strcasecmp(wKey, "slicePlayhead") == 0)
                        r.readColor(outTheme.sliceColors[2]);
                    else
                        r.skipValue();
                    r.match(',');
                }
                r.match('}');
            }
            else
            {
                r.skipValue();
            }
        }
        else if (strcasecmp(key, "accents") == 0)
        {
            if (r.match('{'))
            {
                while (!r.peek('}') && r.pos < r.len)
                {
                    char aKey[32];
                    if (!r.readString(aKey, sizeof(aKey)))
                        break;
                    r.match(':');
                    if (strcasecmp(aKey, "white") == 0)
                        r.readColor(outTheme.white);
                    else if (strcasecmp(aKey, "red") == 0)
                        r.readColor(outTheme.red);
                    else if (strcasecmp(aKey, "orange") == 0)
                        r.readColor(outTheme.orange);
                    else if (strcasecmp(aKey, "yellow") == 0)
                        r.readColor(outTheme.yellow);
                    else if (strcasecmp(aKey, "green") == 0)
                        r.readColor(outTheme.green);
                    else if (strcasecmp(aKey, "cyan") == 0)
                        r.readColor(outTheme.cyan);
                    else if (strcasecmp(aKey, "grey") == 0)
                        r.readColor(outTheme.grey_22);
                    else
                        r.skipValue();
                    r.match(',');
                }
                r.match('}');
            }
            else
            {
                r.skipValue();
            }
        }
        else if (strcasecmp(key, "shadows") == 0)
        {
            if (r.match('{'))
            {
                while (!r.peek('}') && r.pos < r.len)
                {
                    char sKey[32];
                    if (!r.readString(sKey, sizeof(sKey)))
                        break;
                    r.match(':');
                    if (strcasecmp(sKey, "bottomScreen") == 0)
                        r.readColor(outTheme.bottomScreenShadow);
                    else if (strcasecmp(sKey, "bar") == 0)
                        r.readColor(outTheme.barShadow);
                    else if (strcasecmp(sKey, "patternEditorPlayhead") == 0)
                        r.readColor(outTheme.patternEditorPlayheadShadow);
                    else if (strcasecmp(sKey, "patternEditorBottomButtonsLines") == 0)
                        r.readColor(outTheme.patternEditorBottomButtonsLines);
                    else
                        r.skipValue();
                    r.match(',');
                }
                r.match('}');
            }
            else
            {
                r.skipValue();
            }
        }
        else if (strcasecmp(key, "moreButton") == 0)
        {
            r.readMapped(outTheme.moreButtonColors, 4, kMoreButtonMap);
            customMoreButtonSet = true;
        }
        else if (strcasecmp(key, "editBox") == 0)
        {
            r.readMapped(outTheme.editBoxColors, 5, kEditBoxMap);
            customEditBoxSet = true;
        }
        else if (strcasecmp(key, "keyboard") == 0)
        {
            r.readMapped(outTheme.keyboardColors, 3, kKeyboardMap);
            customKeyboardSet = true;
        }
        else if (strcasecmp(key, "performance") == 0)
        {
            r.readPerformance(outTheme);
        }
        else if (strcasecmp(key, "dialog") == 0)
        {
            r.readMapped(outTheme.popupTextColors, 3, kTripleMap);
            customDialogSet = true;
        }
        else if (strcasecmp(key, "miniMenu") == 0)
        {
            r.readMapped(outTheme.miniBoxMenuColors, 7, kMiniMenuMap);
            customMiniMenuSet = true;
        }
        else
        {
            r.skipValue();
        }

        r.match(',');
    }

    if ((customActiveLabelsSet || customInactiveLabelsSet) && !customDefaultListSet)
    {
        if (!customActiveListSet)
        {
            if (customActiveLabelsSet)
            {
                outTheme.activeListColors[0] = menuActiveColors[2];
                outTheme.activeListColors[1] = menuActiveColors[2];
                outTheme.activeListColors[3] = customListBgSet ? customListBg : menuActiveColors[1];
                outTheme.activeListColors[5] = menuActiveColors[0];
            }
            outTheme.activeListColors[2] = outTheme.stepPopupListColor[2];
            outTheme.activeListColors[4] = customInactiveLabelsSet
                                             ? menuInactiveColors[0]
                                             : (customActiveLabelsSet ? menuActiveColors[0] : outTheme.activeListColors[4]);
            outTheme.activeListColors[6] = outTheme.stepPopupListColor[6];
        }

        if (!customInactiveListSet)
        {
            if (customInactiveLabelsSet)
            {
                outTheme.inactiveListColors[0] = menuInactiveColors[2];
                outTheme.inactiveListColors[1] = customActiveLabelsSet ? menuActiveColors[2] : menuInactiveColors[2];
                outTheme.inactiveListColors[3] = customListBgSet ? customListBg : menuInactiveColors[1];
                outTheme.inactiveListColors[4] = menuInactiveColors[0];
                outTheme.inactiveListColors[5] = menuInactiveColors[0];
            }
            outTheme.inactiveListColors[2] = outTheme.activeListColors[2];
            outTheme.inactiveListColors[6] = outTheme.activeListColors[6];
        }
    }

    if (customActiveLabelsSet)
    {
        outTheme.configItemEditorTextLabelColors[0] = menuActiveColors[0];
        outTheme.configItemEditorTextLabelColors[1] = customBgSet ? outTheme.backgroundColor : menuActiveColors[1];
        outTheme.configItemEditorTextLabelColors[2] = outTheme.frameColors[0];
        outTheme.configItemEditorTextLabelColors[3] = menuActiveColors[0];
    }

    if (customActiveButtonSet || customBottomButtonTextSet || customBottomButtonBgSet)
    {
        uint32_t btnText = customBottomButtonTextSet ? bottomButtonText : outTheme.activeButtonLabelsColors[0];
        uint32_t btnBg = customActiveButtonSet
                           ? outTheme.activeButtonLabelsColors[1]
                           : (customBottomButtonBgSet ? outTheme.bottomButtonsBgColors[1] : outTheme.activeButtonLabelsColors[1]);
        uint32_t btnFrame = outTheme.activeButtonLabelsColors[2];
        uint32_t btnValText = outTheme.activeButtonLabelsColors[3];

        outTheme.activeLabelsColors[0] = btnText;
        outTheme.activeLabelsColors[1] = btnBg;
        outTheme.activeLabelsColors[2] = btnFrame;
        outTheme.activeLabelsColors[3] = btnValText;

        outTheme.activeButtonLabelsColors[0] = btnText;
        outTheme.activeButtonLabelsColors[1] = btnBg;
        outTheme.activeButtonLabelsColors[2] = btnFrame;
        outTheme.activeButtonLabelsColors[3] = btnText;
    }

    if (customInactiveButtonSet)
    {
        outTheme.inactiveButtonLabelsColors[3] = outTheme.inactiveButtonLabelsColors[0];
        outTheme.inactiveLabelsColors[0] = outTheme.inactiveButtonLabelsColors[0];
        outTheme.inactiveLabelsColors[1] = outTheme.inactiveButtonLabelsColors[1];
        outTheme.inactiveLabelsColors[2] = outTheme.inactiveButtonLabelsColors[2];
        outTheme.inactiveLabelsColors[3] = outTheme.inactiveButtonLabelsColors[0];
    }

    if (!customStepDescSet)
    {
        outTheme.stepPopupDescriptionTextBoxColors[0] = outTheme.stepPopupListColor[4];
        outTheme.stepPopupDescriptionTextBoxColors[1] = customStepBgSet ? outTheme.stepPopupBgLabelColor[1] : outTheme.stepPopupListColor[3];
        outTheme.stepPopupDescriptionTextBoxColors[2] = outTheme.frameColors[0];
    }

    if (customBgSet)
    {
        // Lists
        if (customListBgSet)
        {
            outTheme.activeListColors[3] = customListBg;
            outTheme.inactiveListColors[3] = customListBg;
            outTheme.projectActiveListColors[3] = customListBg;
            outTheme.projectInactiveListColors[3] = customListBg;
            outTheme.sampleLoaderActiveListColors[3] = customListBg;
            outTheme.sampleLoaderInactiveListColors[3] = customListBg;
            outTheme.stepPopupListColor[3] = customListBg;
        }
        else
        {
            if (outTheme.activeListColors[3] == 0x0A0A0A)
                outTheme.activeListColors[3] = outTheme.backgroundColor;
            if (outTheme.inactiveListColors[3] == 0x0A0A0A)
                outTheme.inactiveListColors[3] = outTheme.backgroundColor;
            if (outTheme.projectActiveListColors[3] == 0x0A0A0A)
                outTheme.projectActiveListColors[3] = outTheme.backgroundColor;
            if (outTheme.projectInactiveListColors[3] == 0x0A0A0A)
                outTheme.projectInactiveListColors[3] = outTheme.backgroundColor;
            if (outTheme.sampleLoaderActiveListColors[3] == 0x0A0A0A)
                outTheme.sampleLoaderActiveListColors[3] = outTheme.backgroundColor;
            if (outTheme.sampleLoaderInactiveListColors[3] == 0x0A0A0A)
                outTheme.sampleLoaderInactiveListColors[3] = outTheme.backgroundColor;
            if (outTheme.stepPopupListColor[3] == 0x0A0A0A)
                outTheme.stepPopupListColor[3] = outTheme.backgroundColor;
        }

        // Bars
        if (customBarBgSet)
        {
            outTheme.activeBarColors[4] = customBarBg;
            outTheme.inactiveBarColors[4] = customBarBg;
            outTheme.sampleLoadMemBarNormalColors[4] = customBarBg;
            outTheme.sampleLoadMemBarFullColors[4] = customBarBg;
        }
        else
        {
            if (outTheme.activeBarColors[4] == 0x0A0A0A)
                outTheme.activeBarColors[4] = outTheme.backgroundColor;
            if (outTheme.inactiveBarColors[4] == 0x0A0A0A)
                outTheme.inactiveBarColors[4] = outTheme.backgroundColor;
            if (outTheme.sampleLoadMemBarNormalColors[4] == 0x0A0A0A)
                outTheme.sampleLoadMemBarNormalColors[4] = outTheme.backgroundColor;
            if (outTheme.sampleLoadMemBarFullColors[4] == 0x0A0A0A)
                outTheme.sampleLoadMemBarFullColors[4] = outTheme.backgroundColor;
        }

        if (customBarContainerSet)
        {
            outTheme.activeBarColors[3] = customBarContainer;
            outTheme.inactiveBarColors[3] = customBarContainer;
            outTheme.sampleLoadMemBarNormalColors[3] = customBarContainer;
            outTheme.sampleLoadMemBarFullColors[3] = customBarContainer;
        }
        else
        {
            if (outTheme.activeBarColors[3] == 0x080808)
                outTheme.activeBarColors[3] = outTheme.backgroundColor;
            if (outTheme.inactiveBarColors[3] == 0x080808)
                outTheme.inactiveBarColors[3] = outTheme.activeBarColors[3];
            if (outTheme.sampleLoadMemBarNormalColors[3] == 0x080808)
                outTheme.sampleLoadMemBarNormalColors[3] = outTheme.activeBarColors[3];
            if (outTheme.sampleLoadMemBarFullColors[3] == 0x080808)
                outTheme.sampleLoadMemBarFullColors[3] = outTheme.activeBarColors[3];
        }

        if (!customInactiveBarSet)
        {
            outTheme.inactiveBarColors[0] = menuInactiveColors[0];
            outTheme.inactiveBarColors[1] = outTheme.activeBarColors[1];
            outTheme.inactiveBarColors[2] = outTheme.activeBarColors[2];
            outTheme.inactiveBarColors[3] = outTheme.activeBarColors[3];
            outTheme.inactiveBarColors[4] = outTheme.activeBarColors[4];
        }

        // Blank space cover
        if (!customBlankSpaceSet)
        {
            outTheme.blankSpaceCoverLabelColors[1] = outTheme.backgroundColor;
        }

        // Step popup bg
        if (!customStepBgSet)
        {
            outTheme.stepPopupBgLabelColor[0] = outTheme.stepPopupListColor[5];
            outTheme.stepPopupBgLabelColor[1] = outTheme.stepPopupListColor[3];
        }

        // Edit box
        if (!customEditBoxSet)
        {
            outTheme.editBoxColors[0] = menuActiveColors[0];
            outTheme.editBoxColors[1] = menuActiveColors[1];
            outTheme.editBoxColors[2] = outTheme.frameColors[0];
            outTheme.editBoxColors[3] = outTheme.highlightedButtonLabelsColors[1];
            outTheme.editBoxColors[4] = outTheme.backgroundColor;
        }

        // Keyboard
        if (!customKeyboardSet)
        {
            outTheme.keyboardColors[0] = menuActiveColors[1];
            outTheme.keyboardColors[1] = menuActiveColors[0];
            outTheme.keyboardColors[2] = outTheme.frameColors[0];
        }

        // Performance
        if (outTheme.perfValueNormalMLabelColors[1] == 0x0A0A0A)
            outTheme.perfValueNormalMLabelColors[1] = outTheme.backgroundColor;
        if (outTheme.perfValueEditMLabelColors[1] == 0x0A0A0A)
            outTheme.perfValueEditMLabelColors[1] = outTheme.backgroundColor;
        if (outTheme.perfFxNameTextLabelColors[1] == 0x121212)
            outTheme.perfFxNameTextLabelColors[1] = outTheme.backgroundColor;

        // Dialog popup
        if (!customDialogSet && outTheme.popupTextColors[1] == 0x222222)
        {
            outTheme.popupTextColors[1] = outTheme.stepPopupBgLabelColor[1];
        }
        if (outTheme.configConfirmPopUpLabelColors[1] == 0x222222)
        {
            outTheme.configConfirmPopUpLabelColors[0] = menuActiveColors[0];
            outTheme.configConfirmPopUpLabelColors[1] = outTheme.stepPopupBgLabelColor[1];
            outTheme.configConfirmPopUpLabelColors[2] = outTheme.frameColors[0];
            outTheme.configConfirmPopUpLabelColors[3] = menuActiveColors[0];
        }

        // More button
        if (!customMoreButtonSet)
        {
            outTheme.moreButtonColors[0] = outTheme.activeButtonLabelsColors[0];
            outTheme.moreButtonColors[1] = outTheme.activeButtonLabelsColors[1];
            outTheme.moreButtonColors[2] = outTheme.moreButtonColors[0];
            outTheme.moreButtonColors[3] = menuInactiveColors[0];
        }
        else
        {
            if (outTheme.moreButtonColors[2] == 0)
                outTheme.moreButtonColors[2] = outTheme.moreButtonColors[0];
            if (outTheme.moreButtonColors[3] == 0)
                outTheme.moreButtonColors[3] = menuInactiveColors[0];
        }

        // Config item editor label
        if (outTheme.configItemEditorTextLabelColors[1] == 0x0A0A0A)
            outTheme.configItemEditorTextLabelColors[1] = outTheme.backgroundColor;

        // Waveform points
        if (outTheme.waveformPointsColors[1] == 0x0A0A0A)
            outTheme.waveformPointsColors[1] = outTheme.backgroundColor;

        // Mini box menu overlay (Tracker Mini menu button overlay)
        if (!customMiniMenuSet)
        {
            outTheme.miniBoxMenuColors[0] = menuActiveColors[0];
            outTheme.miniBoxMenuColors[1] = menuActiveColors[1];
            outTheme.miniBoxMenuColors[2] = outTheme.highlightedButtonLabelsColors[1];
            outTheme.miniBoxMenuColors[3] = outTheme.backgroundColor;
            outTheme.miniBoxMenuColors[4] = outTheme.frameColors[0];
            outTheme.miniBoxMenuColors[5] = menuInactiveColors[0];
            outTheme.miniBoxMenuColors[6] = menuActiveColors[0];
        }
    }

    return true;
}

void ThemeManager::init(Platform* p)
{
    platform = p;
    scanThemesAndFonts();
}

void ThemeManager::scanThemesAndFonts()
{
    scanThemes();
    scanFonts();
}

static void scanAssetDirectory(
  Platform* platform,
  const char* dirName,
  const char* dirNameSlash,
  bool (*filterFn)(const char*),
  const char* ext,
  size_t extLen,
  uint8_t& count,
  int maxCount,
  char names[][ThemeManager::kMaxNameLength],
  const char* namePointers[],
  char filePaths[][64],
  char* namesBuffer,
  size_t namesBufferSize,
  char* fileList[])
{
    if (!platform)
        return;
    FileSystem* fs = platform->getFileSystem();
    if (!fs || !fs->isSdCardInserted())
        return;

    const char* dirToScan = nullptr;
    if (fs->exists(dirName))
    {
        dirToScan = dirName;
    }
    else if (fs->exists(dirNameSlash))
    {
        dirToScan = dirNameSlash;
    }
    else
    {
        fs->mkdir(0, dirName);
        if (fs->exists(dirName))
            dirToScan = dirName;
        else if (fs->exists(dirNameSlash))
            dirToScan = dirNameSlash;
    }

    if (!dirToScan)
        return;

    int outCount = maxCount - count;
    fs->listDirContents(
      dirToScan,
      FileSystem::DirContentType::kFiles,
      filterFn,
      namesBuffer,
      namesBufferSize,
      ThemeManager::kMaxNameLength,
      outCount,
      fileList);

    for (int i = 0; i < outCount && count < maxCount; i++)
    {
        const char* fileName = fileList[i];
        if (!fileName || strlen(fileName) == 0)
            continue;

        snprintf(filePaths[count], 64, "%s/%s", dirToScan, fileName);

        strncpy(names[count], fileName, ThemeManager::kMaxNameLength - 1);
        names[count][ThemeManager::kMaxNameLength - 1] = '\0';
        size_t nlen = strlen(names[count]);
        if (nlen > extLen && strcasecmp(names[count] + nlen - extLen, ext) == 0)
        {
            names[count][nlen - extLen] = '\0';
        }

        namePointers[count] = names[count];
        count++;
    }
}

void ThemeManager::scanThemes()
{
    ensureDefaultsInitialized();
    themeCount = 3;
    themeNamePointers[0] = themeNames[0];
    themeNamePointers[1] = themeNames[1];
    themeNamePointers[2] = themeNames[2];

    scanAssetDirectory(
      platform,
      "Themes",
      "/Themes",
      [](const char* name) { return hasExtension(name, ".json"); },
      ".json",
      5,
      themeCount,
      kMaxCustomThemes,
      themeNames,
      themeNamePointers,
      themeFilePaths,
      s_themeNamesBuffer,
      sizeof(s_themeNamesBuffer),
      s_themeFileList);
}

void ThemeManager::scanFonts()
{
    ensureDefaultsInitialized();
    fontCount = 2;
    fontNamePointers[0] = fontNames[0];
    fontNamePointers[1] = fontNames[1];

    scanAssetDirectory(
      platform,
      "Fonts",
      "/Fonts",
      [](const char* name) { return hasExtension(name, ".ptfont"); },
      ".ptfont",
      7,
      fontCount,
      kMaxCustomFonts,
      fontNames,
      fontNamePointers,
      fontFilePaths,
      s_fontNamesBuffer,
      sizeof(s_fontNamesBuffer),
      s_fontFileList);
}

uint8_t ThemeManager::getThemeCount()
{
    ensureDefaultsInitialized();
    return themeCount;
}

const char* const* ThemeManager::getThemeNames()
{
    ensureDefaultsInitialized();
    return themeNamePointers;
}

const char* ThemeManager::getCurrentThemeName()
{
    ensureDefaultsInitialized();
    if (currentThemeIndex < themeCount)
        return themeNamePointers[currentThemeIndex];
    return "Original";
}

bool ThemeManager::applyTheme(uint8_t index)
{
    ensureDefaultsInitialized();
    if (index < 3)
    {
        currentThemeIndex = index;
        display.setTheme(index);
        mtInterface.refreshInterfaceControlsColors();
        return true;
    }

    if (index >= themeCount || !platform)
    {
        currentThemeIndex = 0;
        display.setTheme(0);
        mtInterface.refreshInterfaceControlsColors();
        return false;
    }

    FileSystem* fs = platform->getFileSystem();
    if (!fs || !fs->isSdCardInserted())
    {
        currentThemeIndex = 0;
        display.setTheme(0);
        mtInterface.refreshInterfaceControlsColors();
        return false;
    }

    size_t bytesRead = 0;
    if (!SdTextFile::readAll(fs, themeFilePaths[index], s_fileReadBuffer, sizeof(s_fileReadBuffer), &bytesRead) ||
        !parseThemeJson(s_fileReadBuffer, bytesRead, themeCustom))
    {
        currentThemeIndex = 0;
        display.setTheme(0);
        mtInterface.refreshInterfaceControlsColors();
        return false;
    }

    currentThemeIndex = index;
    display.setCustomTheme(themeCustom);
    mtInterface.refreshInterfaceControlsColors();
    return true;
}

uint8_t ThemeManager::getFontCount()
{
    ensureDefaultsInitialized();
    return fontCount;
}

const char* const* ThemeManager::getFontNames()
{
    ensureDefaultsInitialized();
    return fontNamePointers;
}

const char* ThemeManager::getCurrentFontName()
{
    ensureDefaultsInitialized();
    if (currentFontIndex < fontCount)
        return fontNamePointers[currentFontIndex];
    return "Original";
}

bool ThemeManager::applyFont(uint8_t index)
{
    if (index < 2)
    {
        currentFontIndex = index;
        display.setFont(index);
        return true;
    }

    if (index >= fontCount || !platform)
    {
        currentFontIndex = 0;
        display.setFont(0);
        return false;
    }

    FileSystem* fs = platform->getFileSystem();
    if (!fs || !fs->isSdCardInserted())
    {
        currentFontIndex = 0;
        display.setFont(0);
        return false;
    }

    if (display.loadCustomFontFromSD(fs, fontFilePaths[index]))
    {
        currentFontIndex = index;
        return true;
    }

    currentFontIndex = 0;
    display.setFont(0);
    return false;
}
