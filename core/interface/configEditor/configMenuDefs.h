#ifndef INTERFACE_CONFIGEDITOR_CONFIGMENUDEFS_H_
#define INTERFACE_CONFIGEDITOR_CONFIGMENUDEFS_H_

#include "mtStructs.h"
#include "scales.h"
#include <stdint.h>

typedef enum enMenuType
{
    menuTypeEmpty,
    menuTypeGroup,
    menuTypeItem,

} menu_t;

typedef enum enMenuItemType
{
    menuItemTypeEmpty,
    menuItemTypeValueU8,
    menuTypeItemListValues,
    menuTypeItemListText,
    menuTypeItemListTextWithAction,
    menuTypeItemLabel,
    menuTypeItemLabelPtr,
    menuTypeItemActionButton,

} menu_item_t;

struct strItemTypeValue8
{
    uint8_t* value;
    uint8_t min;
    uint8_t max;
    uint8_t interval; // narazie nie uzywane
    uint8_t dev;
};

struct strItemTypeListValues
{
    uint8_t* value;
    uint8_t min;
    uint8_t max;
    uint8_t interval; // narazie nie uzywane
    uint8_t dev;
};

struct strItemTypeListText
{
    uint8_t* value;
    uint8_t count;
    const char* const* ptrText;
};

struct strItemTypeListTextWithAction
{
    uint8_t* value;
    uint8_t count;
    const char* const* ptrText;
    void (*funct1)(void) = nullptr;
};

struct strItemTypeLabel
{
    const char* text;
};

struct strItemTypeLabelPtr
{
    const char** text;
};

struct strItemTypeActionButton
{
    void (*funct1)(void) = nullptr;
    void (*funct2)(void) = nullptr;
};

const char emptyName[5] = "";
const char* const ptrEmptyName = emptyName;

//===========================================================
//===========================================================
//===========================================================
// templates

const char* const onOffStateTexts[2] = {
    "Off",
    "On",
};

// general

const char* const brightness[3] = {
    "Low",
    "Medium",
    "High",
};

const char* const themeChoices[3] = {
    "Original",
    "Monochrome",
    "Anniversary",
};

const char* const ptrFont[2] = {
    "Original",
    "New",
};

const char* const radioRegion[4] = {
    "Europe",
    "US",
    "Australia",
    "Japan",
};

const char* const recOptionsTexts[4] = {
    "Only notes",
    "Microtiming",
    "Velocity",
    "Microtiming + Velocity",
};

const char* const recThresholdTexts[16] = {
    "0",
    "-4",
    "-8",
    "-12",
    "-16",
    "-20",
    "-24",
    "-28",
    "-32",
    "-36",
    "-40",
    "-44",
    "-48",
    "-52",
    "-56",
    "-60",
};

const char* const performanceSourceTexts[2] = {
    "Global",
    "Per Project",
};

const char* const padsLayoutTexts[12] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
};

const char* const limiterModeTexts[2] = {
    "Subtle",
    "Extreme",
};

const char* const sendsModeTexts[2] = { "Pre-fader", "Post-fader" };

const char* const numberingModeTexts[3] = {
    "Decimal, Steps from 0",
    "Decimal, Steps from 1",
    "Hexadecimal",
};

const char* const padPreviewInPatternEditTexts[3] = {
    "Off",
    "Track",
    "Pattern",
};

const char* const patternArrangementTexts[3] = {
    "Vertical Revolver",
    "Vertical Original",
    "Horizontal",
};

const char* const patternInfoTexts[3] = {
    "Additional info",
    "Track names",
    "Off",
};

const char* const noteDisplayModeTexts[2] = {
    "Original",
    "Traditional",
};

// metronome

const char* const metroTimeSig[12] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
};

//===========================================================
//===========================================================
//===========================================================
// midi

const char* const clockIn[3] = {
    "Internal",
    "USB",
    "MIDI In jack",
};

const char* const midiOut[4] = {
    "Off",
    "USB",
    "MIDI Out jack",
    "USB + MIDI Out jack",
};

const char* const midiIn[4] = {
    "Off",
    "USB",
    "MIDI In jack",
    "USB + MIDI In jack",
};

const char* const middleC[4] = {
    "C-3",
    "C-4",
    "C-5",
    "C-6",
};

const char* const notesChannel[17] = {
    "ALL", "CH1",  "CH2",  "CH3",  "CH4",  "CH5",  "CH6",  "CH7",  "CH8",
    "CH9", "CH10", "CH11", "CH12", "CH13", "CH14", "CH15", "CH16",
};

const char* const slaveClockComp[kExternalClockCompMaxVal + 1] = {
    "-6", "-5", "-4", "-3", "-2", "-1", "0", "1", "2", "3", "4", "5", "6",
};

const char* const kUsbAudioList[4] = {
    "Off",
    "Input (2 in)",
#ifdef PLATFORM_IMX
    "Output (28 out)",
    "In/Out (2 in, 28 out)",
#else
    "Output (2 out)",
    "In/Out (2 in, 2 out)",
#endif
};
//############################################################################################
//						OPISY PARAMETROW MENU CONFIG
//############################################################################################

const char pattDivDesc[] = {
    "Highlights each chosen step, to give you better visual feedback in the pattern. "
};

const char radioRegDesc[] = { "Adjusts radio frequencies to the chosen region." };

const char displBrightDesc[] = { "Sets display brightness. " };

const char displThemeDesc[] = { "Selects the color scheme of the user interface." };

const char displFontDesc[] = { "Selects the font of the user interface." };

const char padsBrightDesc[] = { "Sets the brightness of the pad lights in the grid." };

const char recNotesDesc[] = {
    "Sets what information is stored when recording: quantized notes only, notes with microtiming, "
    "quantized notes with velocity, or notes with microtiming and velocity. You need to record "
    "with an external MIDI device "
    "to record velocity. "
};

const char recThresholdDesc[] = {
    "Defines in microtiming units, how soon notes should be captured by live recording. This is useful when you play too early during a preroll."
};

const char perfPresDesc[] = { "You can store your Perform presets globally (the same settings for "
                              "all projects) or locally (each project has its own presets)." };

#ifdef TRACKER_MINI
const char scaleDesc[] = {
    "The selected scale applies to the Note popup display and the notes generated "
    "by the Random Note FX. All scales are based on equal temperament tuning."
};
#else
const char scaleDesc[] = {
    "All scales use equal temperament tuning. Use the chromatic scale if you want the pad board to "
    "use all twelve semitones. "
    "This gives you four full octaves with the base note in the first column of each line. "
};
#endif

const char rootNoteDesc[] = { "Defines the note at the bottom-left of the Note popup." };

const char padLayDesc[] = {
    "For the first (leftmost) column in the Note popup, this value sets how many semitones higher "
    "should each note in the row above be compared to the row below. For example, "
    "a value of 3 will make the note above the Root the 4th note of the scale (1 + 3), "
    "the note two rows above the Root the 7th note (1 + 3 + 3), and so on. "
    "Use this setting to customise your Note popup for more convenient note selection. "
};

const char antialDesc[] = { "Disabling anti-aliasing gives the vintage feeling. The samples are "
                            "harsher when detuned heavily. " };

const char limiterDesc[] = { "Determines how attack and release of the limiter are treated. Subtle "
                             "limiting gives a more transparent sound but will "
                             "not be able to catch very sudden changes in amplitude. Extreme "
                             "limiting reacts much faster which colors the sound. " };

const char sendsDesc[] = { "Pre-fader sends are independent of the instrument volume. This allows "
                           "the wet signal to be louder than the dry signal. "
                           "Post-fader sends are proportional to the instrument volume. This gives "
                           "a more consistent sound when volume is modulated. " };

const char headroomDesc[] = {
    "Enables more headroom in the audio signal path by reducing the input gain of sample playback "
    "at the source to avoid clipping and improve overall dynamic range."
};

const char snapToZeroDesc[] = {
    "When setting Start/End markers in Sample Playback editor, sudden sample amplitude changes can "
    "cause pops and clicks. To avoid this, activate this mode to automatically snap the markers "
    "when they are set close to the zero amplitude points which are indicated with the green "
    "marker color."
};

const char hexModeDesc[] = { "Enable the throwback hex goodness: pattern step numbers start with "
                             "00 and use sixteen digits (A means 10, B means 11, and so "
                             "on all the way to F which means 15). This allows for easy alignment "
                             "with music: your music bars will always start with pattern "
                             "steps 00, 10, 20, 30, ... Each bar's quarternote beat steps end with "
                             "the digits ..0, ..4, ..8, and ..C. " };

const char patternArraDesc[] = { "Pattern arrangement type" };

const char patternInfo[] = { "Pattern info" };

const char noteDisplayModeDesc[] = {
    "Choose how notes are displayed in Pattern View: Original (C4, C#4) or Traditional (C-4, C#4)."
};

const char usbAudioDesc[] = {
    "Enables audio over USB. For best results connect the device directly to your computer USB "
    "port (USB hubs are not supported) before launching your DAW and set it up as an aggregate "
    "audio device. Find more details in our user manual at polyend.com"
};

const char padsPrevDesc[] = { "On the Pattern editor screen, the grid pads can highlight the note "
                              "in the active line. Choose between highlighting only the "
                              "active track's notes, or the entire pattern's active line. " };

const char firmUpdDesc[] = {

    "Before updating save\n"
    "your current project\n"
    "to prevent any\n"
    "potential data loss.\n"
    "After update create\n"
    "a new project\n"
    "to initialize\n"
    "the Config\n"
    "settings correctly.\n\n"

    "The files listed\n"
    "here are in the\n"
    "Firmware/ dir\n"
    "on your SD card.\n\n"

    "Find new firmware\n"
    "versions on\n"
    "polyend.com."
};

const char slaveClockCompDesc[] = { "Allows compensating latency of incoming clock signal by "
                                    "number of clock pulses, where 1 pulse = 1/6 of step. "
                                    "Use this with \"Clock in\" set to MIDI IN jack or USB. " };

const char supportedCcInDesc[] = { "Supported incoming CC\nmessages:\n\nCC41 - CC48\n(performance "
                                   "mode track),\n\nCC51 - CC62\n(performance mode slot),"
                                   "\n\nCC71 - CC82\n(master mixer levels)." };

#ifdef TRACKER_MINI

const char setupFunctionKeysDesc[] = { "" };

#endif

#endif /* INTERFACE_CONFIGEDITOR_CONFIGMENUDEFS_H_ */
