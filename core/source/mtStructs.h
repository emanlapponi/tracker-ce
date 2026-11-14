#ifndef MT_STRUCTS_H
#define MT_STRUCTS_H

#include "config.h"
#include "mtEnvelopeGenerator.h"
#include <stdint.h>

//=====================================================================
//=====================================================================
//=====================================================================

#ifndef FW_EDITION_NAME
#define FW_EDITION_NAME "CE"
#endif
#ifndef FW_VER_MAJOR
#define FW_VER_MAJOR 1
#endif
#ifndef FW_VER_MINOR
#define FW_VER_MINOR 0
#endif
#ifndef FW_VER_PATCH
#define FW_VER_PATCH 0
#endif
#ifndef FW_VER_BETA
#define FW_VER_BETA 255
#endif

const uint8_t CORE_VER_1 = FW_VER_MAJOR;
const uint8_t CORE_VER_2 = FW_VER_MINOR;
const uint8_t CORE_VER_3 = FW_VER_PATCH;
const uint8_t CORE_BETA = FW_VER_BETA; // beta version; 255 == official release

// version shown in UI
const uint8_t PRODUCT_VER_1 = FW_VER_MAJOR;
const uint8_t PRODUCT_VER_2 = FW_VER_MINOR;
const uint8_t PRODUCT_VER_3 = FW_VER_PATCH;
const uint8_t PRODUCT_BETA = FW_VER_BETA;

//=====================================================================
const char firmwareVersionLabelFormat[] = "%d.%d.%d " FW_EDITION_NAME;
const char firmwareVersionLabelFormatBeta[] = "%d.%d.%d " FW_EDITION_NAME " beta %d";

const uint8_t PROJECT_FILE_VERSION = 17;   // wersja struktury pliku projektu
const uint8_t INSTRUMENT_FILE_VERSION = 9; // wersja struktury pliku instrumentu
const uint8_t PATTERN_FILE_VERSION = 6;

const uint8_t CONFIG_STRUCT_VER = 10;

static constexpr float DEFAULT_TEMPO = 130.0f;
static constexpr float kMinExternalBPM = 10.0f;
static constexpr float kMaxExternalBPM = 999.0f;

const uint16_t MAX_16BIT = 65535;
const int16_t MAX_SIGNED_16BIT = 32767;
const int16_t MIN_SIGNED_16BIT = -32768;
const uint8_t MAX_8BIT = 255;

const float MAX_WAVE_FLOAT = 1.0;
const float MIN_WAVE_FLOAT = -1.0;

#ifndef PLATFORM_IMX
// T1
const uint32_t EXTERAL_MEMORY_ADDRESS = 0x70000000;
const uint32_t TOTAL_EXTERNAL_MEMORY = (16 * 1024 * 1024); // 16 MB
const uint32_t SAMPLE_MEMORY_SIZE = (45 * 256 * 1024);     // 11,25 MB = 133.7469 sec = 2.229 min
#else
// Tmini / Tplus
const uint32_t EXTERAL_MEMORY_ADDRESS = 0x80000000;
const uint32_t TOTAL_EXTERNAL_MEMORY = (64 * 1024 * 1024); // 64 MB
const uint32_t PSYN_RAM_POOL_SIZE = (6 * 1024 * 1024);      // 6 MB for .psyn plugins & synth SamplePool
// ~54 MB: 60 MB minus 64 KB for pattern buffers, 96 KB for the display lists kept in the MCU and the pool above
const uint32_t SAMPLE_MEMORY_SIZE = ((60 * 1024 - 64 - 96) * 1024) - PSYN_RAM_POOL_SIZE;
#endif

const uint32_t DELAY_MEMORY_SIZE = (3 * 512 * 1024);               // 1,5 MB
const uint32_t REVERB_MEMORY_SIZE = 32768;                         // 32 KB
const uint32_t SHARED_SCRATCH_MEMORY_SIZE = 768 * 1024; // 768 KB shared external scratch (.scratch_memory)

const uint32_t EDIT_BUFF_SIZE = (15 * 256 * 1024); // 3,75 MB

#ifdef PLATFORM_IMX
constexpr int SAMPLE_CHANNELS = 2;
#else
constexpr int SAMPLE_CHANNELS = 1;
#endif

const uint32_t EDIT_BUFF_OFFSET = SAMPLE_MEMORY_SIZE - (2 * SAMPLE_CHANNELS * EDIT_BUFF_SIZE);

static constexpr int kSampleTracks = 8;
static constexpr int kMidiTrackFirstIdx = kSampleTracks;
static constexpr int kMidiTracks = 8;
static constexpr int kAllTracks = kSampleTracks + kMidiTracks;

static constexpr int k16TracksPatternSize_v5 = 12336;
static constexpr int k12TracksPatternSize_v5 = 9260;
static constexpr int k16TracksPatternSize = 16432;
static constexpr int k12TracksPatternSize = 12332;

static constexpr int kMaxMidiTracksInTrackers =
  8; // taking into account all tracker types mini/+/OG
static constexpr int kMaxAllTracksInTrackers =
  kSampleTracks + kMaxMidiTracksInTrackers; // taking into account all tracker types mini/+/OG

// this const for now indicates places where tracks are used in performance context
static constexpr int kPerformanceTracks = kAllTracks;

static constexpr uint8_t kSampleInstrumentsCount = 48;
static constexpr uint8_t kMidiInstrumentsCount = 16;
static constexpr uint8_t kSynthInstrumentsCount = 3;
static constexpr uint8_t kTotalInstrumentsCount =
#ifdef FEATURE_SYNTH
  kSynthInstrumentsCount +
#endif
  kMidiInstrumentsCount + kSampleInstrumentsCount;

static constexpr int kSynthMacrosCount = 6;

static constexpr int kCCMinValue = 0;
static constexpr int kCCMidValue = 63;
static constexpr int kCCMaxValue = 127;

const uint8_t MIDI_INSTRUMENTS_START_IDX = kSampleInstrumentsCount;
const uint8_t SYNTH_INSTRUMENTS_START_IDX = MIDI_INSTRUMENTS_START_IDX + kMidiInstrumentsCount;

const uint8_t PATTERN_INDEX_MIN = 1;
const uint8_t PATTERN_INDEX_MAX = 255;

const uint8_t FX_MAX = 49;
const uint8_t FX_COUNT = 46;
const uint8_t FX_COUNT_HIDDEN_FXes = 1; // ile fxów na końcu listy jest ukrytych

const uint8_t FX_VALUE_MAX = 127;

const uint8_t PATTERN_EDIT_STEP_MAX = 32;

const uint8_t FX_SLOTS_MAX = 1;
const uint8_t FX_SLOTS_COUNT = 2;

const uint8_t PROJECT_NAME_SIZE = 33;
const uint8_t INSTRUMENT_NAME_SIZE = 19;
const uint8_t PATTERN_NAME_LENGTH = 30;
const uint8_t TRACK_NAME_LENGTH = 7;
const uint8_t SAMPLE_NAME_SIZE = 31;
const uint8_t FX_NAME_SIZE = 32;
const uint8_t GRID_NAME_SIZE = 32;

const uint8_t PATCH_SIZE = 255;
const uint8_t SONG_MAX = 255;

const uint8_t SAMPLE_POINT_POS_MIN = 0;
const uint16_t SAMPLE_POINT_POS_MAX = MAX_16BIT;

const int8_t MAX_NOTE = 120;
const int8_t MIN_NOTE = 0;

const uint8_t DEFAULT_MIDI_VELOCITY = 127;
const uint8_t MIDI_CC_MODWHEEL = 1;
const uint8_t MIDI_CC_GLIDE = 5;
const uint8_t MIDI_CC_CHAN_VOLUME = 7;
const uint8_t MIDI_CC_EXPRESSION = 11;
const uint8_t MIDI_CC_SUSTAIN = 64;
const uint8_t MIDI_CC_SPECIAL_PC = 128;
const uint8_t MIDI_CC_SPECIAL_CHAN_AT = 129;
const uint8_t MIDI_CC_SPECIAL_POLY_AT = 130;

const uint8_t INSTRUMEN_ENVELOPES_MAX = 3;

const float MIN_PITCH = 0.25;
const float MAX_PITCH = 4.0;

const int8_t PANNING_MIN = 0;
const uint8_t PANNING_MAX = 100;
const uint8_t GLIDE_MIN = 0;
const uint16_t GLIDE_MAX = 15000;
const uint8_t SEND_MIN = 0;
const uint8_t SEND_MAX = 100;

const uint16_t ATTACK_MAX = 10000; // zastosowane jest dzielenie przez 100 bez floatow wiec wazne
                                   // zeby wartosci czasowe max env byly podzielne przez 100
const uint16_t DECAY_MAX = 10000;
const float SUSTAIN_MAX = 1.0;
const uint16_t RELEASE_MAX = 10000;
const float AMOUNT_MAX = 1.0;
const uint16_t ENVELOPE_MICRO_VAL = 100; // na ile wartosci rozklada sie zakres micro
const uint16_t ENVELOPE_MICRO_RANGE = 1000;

const float RESONANCE_OFFSET = 0.7;
const float RESONANCE_MIN = 0;
const float RESONANCE_MAX = 4.3;

const uint16_t HIGH_PASS_FILTER_FREQ = (14000 / 128);
const uint16_t LOW_PASS_FILTER_FREQ = (14000 / 128);
const float MAX_OCTAVE_CONTROL = 7.0;

const uint8_t MAX_TARGET = 20;
const uint8_t MAX_MOD = 5;

const float MAX_CUTOFF = 1.0;
const float MIN_CUTOFF = 0.0;

const uint8_t OVERDRIVE_MAX = 100;
const uint8_t OVERDRIVE_MIN = 0;

const int8_t MIN_INSTRUMENT_TUNE = -24;
const uint8_t MAX_INSTRUMENT_TUNE = 24;
const int8_t MIN_INSTRUMENT_FINETUNE = -100;
const uint8_t MAX_INSTRUMENT_FINETUNE = 100;
const uint8_t MAX_INSTRUMENT_VOLUME = 100;
const uint8_t MIN_INSTRUMENT_VOLUME = 0;
const uint8_t DEFAULT_INSTRUMENT_VOLUME = 50;

constexpr uint8_t ENVELOPE_SPEED_AMP_COUNT = 24;
constexpr uint8_t ENVELOPE_SPEED_OTHER_COUNT = 29;

constexpr uint8_t CONSTRAIN_SPEED_AMP_ENVELOPE = ENVELOPE_SPEED_AMP_COUNT - 1;
constexpr uint8_t CONSTRAIN_SPEED_OTHER_ENVELOPE = ENVELOPE_SPEED_OTHER_COUNT - 1;

constexpr uint8_t ENVELOPE_SPEED_CONSTRAIN_OFFSET =
  CONSTRAIN_SPEED_OTHER_ENVELOPE - CONSTRAIN_SPEED_AMP_ENVELOPE;

const uint8_t MAX_WAVETABLE_WINDOW = 255;
const uint16_t STANDARD_WAVETABLE_WINDOW_LEN = 1024;
const uint16_t SERUM_WAVETABLE_WINDOW_LEN = 2048;

const uint16_t STANDARD_WAVETABLE_WINDOWS_NUMBER = 256;

constexpr uint32_t LP1_MASK = 0b00000000000000000000000000000001;
constexpr uint32_t LP2_MASK = 0b00000000000000000000000000000010;
constexpr uint32_t FINETUNE_MASK = 0b00000000000000000000000000000100;
constexpr uint32_t TUNE_MASK = 0b00000000000000000000000000001000;
constexpr uint32_t VOLUME_MASK = 0b00000000000000000000000000010000;
constexpr uint32_t PANNING_MASK = 0b00000000000000000000000000100000;
constexpr uint32_t CUTOFF_MASK = 0b00000000000000000000000001000000;
constexpr uint32_t RESONANCE_MASK = 0b00000000000000000000000010000000;
constexpr uint32_t DELAY_SEND_MASK = 0b00000000000000000000000100000000;
constexpr uint32_t WT_POS_SEND_MASK = 0b00000000000000000000001000000000;
constexpr uint32_t GRANULAR_POS_SEND_MASK = 0b00000000000000000000010000000000;
constexpr uint32_t GRANULAR_LEN_SEND_MASK = 0b00000000000000000000100000000000;
constexpr uint32_t GRANULAR_WAVE_SEND_MASK = 0b00000000000000000001000000000000;
constexpr uint32_t GRANULAR_LOOP_SEND_MASK = 0b00000000000000000010000000000000;
constexpr uint32_t LFO_AMP_SEND_MASK = 0b00000000000000000100000000000000;
constexpr uint32_t LFO_FILTER_SEND_MASK = 0b00000000000000001000000000000000;
constexpr uint32_t LFO_WT_POS_SEND_MASK = 0b00000000000000010000000000000000;
constexpr uint32_t LFO_GRAN_POS_SEND_MASK = 0b00000000000000100000000000000000;
constexpr uint32_t LFO_PANNING_SEND_MASK = 0b00000000000001000000000000000000;
constexpr uint32_t LFO_FINETUNE_SEND_MASK = 0b00000000000010000000000000000000;
constexpr uint32_t REVERB_SEND_MASK = 0b00000000000100000000000000000000;
constexpr uint32_t OVERDRIVE_MASK = 0b00000000001000000000000000000000;
constexpr uint32_t BIT_DEPTH_MASK = 0b00000000010000000000000000000000;

const uint8_t MIN_NOTE_OFFSET = 0;
const uint8_t MAX_NOTE_OFFSET = 13;

const uint8_t MASTER_VOLUME_MIN = 0;
const uint8_t MASTER_VOLUME_MAX = 100;
const uint8_t MASTER_VOLUME_DEFAULT = 81;
constexpr float MIN_ANALOG_VOLUME_IN_DB = -51.5f;
constexpr float MAX_ANALOG_VOLUME_IN_DB = 12.0f;

const uint8_t PREVIEW_VOLUME_MIN = 0;
const uint8_t PREVIEW_VOLUME_MAX = 100;
const uint8_t PREVIEW_VOLUME_DEFAULT = 86;

const uint8_t BIT_DEPTH_MIN = 4;
const uint8_t BIT_DEPTH_MAX = 16;

const uint8_t BASS_BOOST_MIN = 0;
const uint8_t BASS_BOOST_MAX = 100;
const uint8_t BASS_BOOST_DEFAULT = 0;

const uint8_t STEREO_BOOST_MIN = 0;
const uint8_t STEREO_BOOST_MAX = 8;
const uint8_t STEREO_BOOST_DEFAULT = 0;

const uint8_t INPUT_MIC_GAIN_DEFAULT = 35;

const uint8_t LINE_IN_SENS_LEVEL_DEFAULT = 15;
const uint8_t LINE_OUT_LEVEL_DEFAULT = 29;

const uint8_t REVERB_ROOM_SIZE_MIN = 0;
const uint8_t REVERB_ROOM_SIZE_MAX = 100;
const uint8_t REVERB_DAMPING_MIN = 0;
const uint8_t REVERB_DAMPING_MAX = 100;
const int8_t REVERB_PANNING_MIN = -100;
const uint8_t REVERB_PANNING_MAX = 100;

const uint8_t DELAY_FEEDBACK_MIN = 0;
const uint8_t DELAY_FEEDBACK_MAX = 99;
const uint16_t DELAY_TIME_MIN = 1;
const uint16_t DELAY_TIME_MAX = 3500;
const uint16_t DELAY_FILTER_FREQ_MIN = 100;
const uint16_t DELAY_FILTER_FREQ_MAX = 20000;

const float REVERB_SIZE_MAX = 1.0f;
const float REVERB_SIZE_MIN = 0.0f;
const float REVERB_DAMP_MAX = 1.0f;
const float REVERB_DAMP_MIN = 0.0f;
const float REVERB_PREDELAY_MAX = 1.0f;
const float REVERB_PREDELAY_MIN = 0.0f;
const float REVERB_DIFFUSION_MAX = 0.85f;
const float REVERB_DIFFUSION_MIN = 0.0f;

const uint8_t TRACK_VOLUME_MAX = 100;
const uint8_t TRACK_VOLUME_MIN = 0;
const uint8_t DEFAULT_TRACK_VOLUME = 86;
const uint8_t REVERB_VOLUME_MAX = 100;
const uint8_t REVERB_VOLUME_MIN = 0;
const uint8_t DEFAULT_REVERB_VOLUME = 86;
const uint8_t DELAY_VOLUME_MAX = 100;
const uint8_t DELAY_VOLUME_MIN = 0;
const uint8_t DEFAULT_DELAY_VOLUME = 86;
const uint8_t DRY_MIX_VOLUME_MAX = 100;
const uint8_t DRY_MIX_VOLUME_MIN = 0;
const uint8_t DEFAULT_DRY_MIX_VOLUME = 86;
const uint8_t LINE_IN_VOLUME_MAX = 100;
const uint8_t LINE_IN_VOLUME_MIN = 0;
const uint8_t DEFAULT_LINE_IN_VOLUME = 86;
const uint8_t LINE_IN_SEND_MAX = 100;

const float DEFAULT_REVERB_SIZE = 0.5f;
const float DEFAULT_REVERB_DAMP = 0.5f;
const float DEFAULT_REVERB_PREDELAY = 0.5f;
const float DEFAULT_REVERB_DIFFUSION = 0.68f;

const uint8_t LIMITER_ATTACK_MIN = 1;
const uint16_t LIMITER_ATTACK_MAX = 1000;
const float LIMITER_RELEASE_MIN = 1;
const float LIMITER_RELEASE_MAX = 1000;
const uint8_t LIMITER_THRESHOLD_MIN = 0;
const uint16_t LIMITER_THRESHOLD_MAX = 32767;
const uint16_t LIMITER_THRESHOLD_DEFAULT = 30200;

const uint8_t LIMITER_SIDECHAIN_MIN = 0;
#ifdef FEATURE_SYNTH
const uint8_t LIMITER_SIDECHAIN_MAX = 10 + kSynthParts;
#else
const uint8_t LIMITER_SIDECHAIN_MAX = 10;
#endif
const uint8_t LIMITER_SIDECHAIN_DEFAULT = 0;

const uint8_t MAX_SLICE_NUMBER = 48;

const uint8_t CONFIG_EEPROM_ADDRESS = 0;

const float ZOOM_MIN = 1;
const float ZOOM_MAX = 10;
const float ZOOM_FACTOR = 0.05;

const int8_t PERFORMANCE_VALUE_MAX = 100;
const int8_t PERFORMANCE_VALUE_MIN = -100;

// CONFIG DEFINES

const uint8_t PADS_LIGHT_BACK_DEFAULT = 10;
const uint8_t PADS_LIGHT_FRONT_DEFAULT = 31;

const uint16_t MIN_GRANULAR_LENGTH = 44;
const uint16_t MAX_GRANULAR_LENGTH = 44100;

const float NOTE_CORRECTION_COEFFICIENT = 44100.0f / 44117.64706f;

constexpr uint8_t FIRST_BEAT_SLICE_NOTE = 24;

static constexpr int kMaxFileMemoryPerRecentProject = 500;
static constexpr int kRecentProjectListLength = 10;

static constexpr int kMaxFileMemoryPerPatternMetadata = 50;

static constexpr int kExternalClockCompMaxVal = 12;
static constexpr int kExternalClockCompDefaultVal = kExternalClockCompMaxVal / 2;

static constexpr int kOpenMetronomeConfig = 1;

//=====================================================================
//=====================================================================
//=====================================================================

enum memoryPlayStatus
{
    successInit = 0,
    badStartPoint,
    badLoopPoint1,
    badLoopPoint2,
    pointsBeyondFile
};

enum instrumentPlayMode
{
    playModeSingleShot = 0,
    playModeLoopForward,
    playModeLoopBackward,
    playModePingpong,
    playModeSlice,
    playModeBeatSlice,
    playModeWavetable, // tylko na uzytek interface - sample type obsługuje to w silniku
    playModeGranular,
    playModeCount
};

enum mtSampleType
{
    mtSampleTypeWaveFile,
    mtSampleTypeWavetable,

    mtSampleTypeCount
};

enum filterType
{
    lowPass,
    highPass,
    bandPass,

    filterTypeMax
};

enum envelopesType
{
    envAmp,
    envPan,
    envCutoff,
    envWtPos,
    // Granular Length shares slot 3 with Wavetable Position because playModeWavetable and
    // playModeGranular are mutually exclusive, preserving the 372-byte strInstrument .pti layout.
    envGranLen = envWtPos,
    envGranPos,
    envFinetune,

    envMax
};

// UI destinations shown in Instrument Automation (2/2)
enum enAutomationDest : uint8_t
{
    autoDestVolume = 0,
    autoDestPanning,
    autoDestCutoff,
    autoDestWtPos,
    autoDestGranPos,
    autoDestGranLen,
    autoDestFinetune,

    autoDestCount
};

static constexpr envelopesType automationDestToEnvSlot(uint8_t destIdx)
{
    switch (destIdx)
    {
        case autoDestVolume: return envAmp;
        case autoDestPanning: return envPan;
        case autoDestCutoff: return envCutoff;
        case autoDestWtPos: return envWtPos;
        case autoDestGranPos: return envGranPos;
        case autoDestGranLen: return envGranLen;
        case autoDestFinetune: return envFinetune;
        default: return envAmp;
    }
}

enum lfoShapeType
{
    lfoShapeReverseSaw,
    lfoShapeSaw,
    lfoShapeTriangle,
    lfoShapeSquare,
    lfoShapeRandom,

    lfoShapeCount,

    lfoShapeDefault = lfoShapeTriangle
};

//---------------------------------------
enum inputSelect
{
    inputSelectLineIn,
    inputSelectMic,

    inputSelectCount
};

enum outputSelect
{
    outputSelectLineOut,
    outputSelectHeadphones,

    outputSelectCount
};
//---------------------------------------

enum fileType
{
    fileTypeProject,
    fileTypeInstrument,
    fileTypePattern,
    fileTypeSample,
};

enum projectType
{
    projectTypeExample,
    projectTypeUserMade
};

//=====================================================================
//=====================================================================
enum enPlaylist
{
    PLAYLIST_EMPTY_SLOT = 0,
};

enum interfaceCommands
{
    interfaceDoNothing,
    interfaceOpenLastProject,

    interfaceCommandsCount
};

enum loaderStateType
{
    loaderStateTypeEnded,
    loaderStateTypeInProgress
};

enum granularShapeType
{
    granularShapeSquare,
    granularShapeTriangle,
    granularShapeGauss,

    granularTypeCount
};

enum granularLoopType
{
    granularLoopForward,
    granularLoopBackward,
    granularLoopPingPong,

    granularLoopCount
};

enum powerStateType
{
    powerStateSleep,
    powerStateRun
};

enum enRecOptions
{
    recOptions_onlyNotes,
    recOptions_microtiming,
    recOptions_velocity,
    recOptions_microtimingAndVelocity,

    recOptions_max = recOptions_microtimingAndVelocity,
};

constexpr uint8_t recThreshold_default = 6; // -24
constexpr uint8_t recThreshold_count = 16;
constexpr uint8_t recThreshold_max = recThreshold_count - 1;

enum limiterMode
{
    limiterModeSubtle,
    limiterModeHard
};

enum sendsMode
{
    sendsModePreFader,
    sendsModePostFader

};

enum padPreviewMode
{
    padPreviewModeOff,
    padPreviewModeTrack,
    padPreviewModePattern
};

enum SaturationType
{
    kSaturationMin,
    kHardClip = kSaturationMin,
    kSoftClip,
    kSaturate,
    kOverdrive,
    kOverdrive2,
    kSaturationTypeEnd
};

//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
struct strInstrument
{
    uint8_t isActive;

    struct strSampleBankSlot
    {
        uint8_t type; //;
        char file_name[32];

        int16_t* address;
        uint32_t length; // = 0;

        uint16_t wavetable_window_size; // = 2048;
        uint32_t wavetableWindowNumber; // = 0;

    } sample;

    char name[4];

    static constexpr char kStreamPrefMagic = 'S';
    bool isStreamPreferred() const { return name[3] == kStreamPrefMagic; }
    void setStreamPreferred(bool streamed) { name[3] = streamed ? kStreamPrefMagic : 0; }

    uint8_t playMode;
    uint16_t startPoint;
    uint16_t loopPoint1;
    uint16_t loopPoint2;
    uint16_t endPoint;

    uint32_t wavetableCurrentWindow;
    //    uint16_t wavetableSync;
    //    uint16_t wavetablePWM;
    //    uint16_t wavetableFlip;
    //    uint16_t wavetableQuantize;

    envelopeGenerator::strEnv envelope[envMax];
    struct strEnvBasedLfo
    {
        uint8_t shape; // = lfoShapeDefault;
        uint8_t speed; // = 0;
        uint8_t wtBend; // = 0; Packed WarpX (high nibble) and WarpY (low nibble)
        uint8_t wtSust; // = 0; Sustain point % along wavetable (0 = one-shot, 1..99 = freeze point)
        float amount;  // = 1.0;
    } lfo[envMax];
    static_assert(sizeof(strEnvBasedLfo) == 8);
    //	LFO::strLfo lfo[lfoMax];

    float cutOff;
    float resonance;
    uint8_t filterType;
    uint8_t filterEnable;

    int8_t tune;
    int8_t fineTune;

    uint8_t volume;

    uint16_t glide;
    int16_t panning; // = 50;

    uint8_t delaySend;
    uint8_t slicePlayThru; // 0 = stop at next slice (default), 1 = play through to sample end

    uint16_t slices[MAX_SLICE_NUMBER];
    uint8_t sliceNumber;
    uint8_t selectedSlice;

    struct strGranular
    {
        uint16_t grainLength; // = 44;
        uint16_t currentPosition;
        uint8_t shape;
        uint8_t type;
    } granular;

    uint8_t reverbSend;
    uint8_t overdrive;
    uint8_t bitDepth; // = BIT_DEPTH_MAX;
    int8_t tuneResample;

    // Bitmasks for envelope[envWtPos / envGranLen].enable so legacy .pti files (which only ever
    // set bit 0 = 0x01 for Wavetable Position) never unintendedly enable Granular Length automation.
    static constexpr uint8_t kEnvEnableWtPosBit = 0x01;
    static constexpr uint8_t kEnvEnableGranLenBit = 0x02;

    bool isWtPosEnvEnabled() const
    {
        return (envelope[envWtPos].enable & kEnvEnableWtPosBit) != 0;
    }

    bool isGranLenEnvEnabled() const
    {
        return (envelope[envGranLen].enable & kEnvEnableGranLenBit) != 0;
    }

    bool isEnvEnabled(uint8_t envIdx) const
    {
        if (envIdx == envWtPos)
        {
            return (playMode == playModeGranular) ? isGranLenEnvEnabled() : isWtPosEnvEnabled();
        }
        return envelope[envIdx].enable != 0;
    }

    bool isAutomationDestEnabled(uint8_t destIdx) const
    {
        if (destIdx == autoDestWtPos)
            return isWtPosEnvEnabled();
        if (destIdx == autoDestGranLen)
            return isGranLenEnvEnabled();
        return envelope[automationDestToEnvSlot(destIdx)].enable != 0;
    }

    void setAutomationDestEnabled(uint8_t destIdx, bool enabled)
    {
        if (destIdx == autoDestWtPos)
        {
            if (enabled)
                envelope[envWtPos].enable |= kEnvEnableWtPosBit;
            else
                envelope[envWtPos].enable &= ~kEnvEnableWtPosBit;
        }
        else if (destIdx == autoDestGranLen)
        {
            if (enabled)
                envelope[envGranLen].enable |= kEnvEnableGranLenBit;
            else
                envelope[envGranLen].enable &= ~kEnvEnableGranLenBit;
        }
        else
        {
            envelope[automationDestToEnvSlot(destIdx)].enable = enabled ? 1 : 0;
        }
    }
};
static_assert(sizeof(strInstrument) == 372, "strInstrument size must remain 372 bytes");

//----------------------------------------------------------------------------------
const strInstrument defaultInstrumentParams = {
    .isActive = 0,
    .sample = { 0, {}, nullptr, 0, 2048, 0 },
    .name = {},
    .playMode = playModeSingleShot,
    .startPoint = 0,
    .loopPoint1 = 1,
    .loopPoint2 = MAX_16BIT - 1,
    .endPoint = MAX_16BIT,
    .wavetableCurrentWindow = 0,
    .envelope = { { 1.0, 0, 10, 0, 0, 1.0, 1000, 0, 1 },
                  { 1.0, 0, 3000, 0, 0, 1.0, 1000, 0, 0 },
                  { 1.0, 0, 3000, 0, 0, 1.0, 1000, 0, 0 },
                  { 1.0, 0, 3000, 0, 0, 1.0, 1000, 0, 0 },
                  { 1.0, 0, 3000, 0, 0, 1.0, 1000, 0, 0 },
                  { 1.0, 0, 3000, 0, 0, 1.0, 1000, 0, 0 } },
    .lfo = { { lfoShapeDefault, 0, 0, 0, 0.5 },
             { lfoShapeDefault, 0, 0, 0, 0.5 },
             { lfoShapeDefault, 0, 0, 0, 0.5 },
             { lfoShapeDefault, 0, 0, 0, 0.5 },
             { lfoShapeDefault, 0, 0, 0, 0.5 },
             { lfoShapeDefault, 0, 0, 0, 0.5 } },
    .cutOff = 1.0,
    .resonance = 0,
    .filterType = lowPass,
    .filterEnable = 0,
    .tune = 0,
    .fineTune = 0,
    .volume = 50,
    .glide = 0,
    .panning = 50,
    .delaySend = 0,
    .slicePlayThru = 0,
    .slices = {},
    .sliceNumber = 0,
    .selectedSlice = 0,
    .granular = { 441, 0, 0, 0 },
    .reverbSend = 0,
    .overdrive = 0,
    .bitDepth = 16,
    .tuneResample = 0,
};

//----------------------------------------------------------------------------------
static constexpr uint32_t kStreamThresholdBytes = 256 * 1024; // 256 KB hybrid streaming threshold
static constexpr uint32_t kStreamPreCacheFrames = 32768;      // 32768 frames (~743 ms at 44.1 kHz)
static constexpr uint32_t kWaveformPeakPoints = 600;
static constexpr uint32_t kClmtElementCount = 64;

inline bool isPlayModeStreamable(uint8_t playMode)
{
    if (playMode == playModeWavetable || playMode == playModeGranular)
    {
        return false;
    }
#ifndef PLATFORM_IMX
    if (playMode == playModeSlice || playMode == playModeBeatSlice)
    {
        return false;
    }
#endif
    return true;
}

struct WaveformPeakPoint
{
    int16_t minVal;
    int16_t maxVal;
};

struct strSampleMemory
{
    enum Channel
    {
        kLeft,
        kRight,
        kCount
    };

    int16_t* address[kCount];
    bool isStreamed = false;
    bool isStereo = false;
    bool hasValidClmt = false;
    bool hasWaveformPeaks = false;
    uint32_t ramFrames = 0;          // Frames allocated per channel in externalRamSampleMemory
    uint32_t preCacheFrames = 0;     // Attack pre-cache frames in address[ch]
    uint32_t fileDataOffset = 0;     // Byte offset to start of audio data in backing file
    uint32_t databaseSector = 0;     // FATFS database sector for fast LBA lookup
    uint32_t sectorsPerCluster = 0;  // FATFS csize (sectors per cluster)
    uint32_t sampleHash = 0;         // Content hash of pre-cache + length for ring buffer reuse
};

typedef struct
{
    float gainDb;
    int32_t voiceCount;
    int32_t synthType;
    float panning;
    char patchName[32];

    float reverbSend;
    float delaySend;

    uint8_t macroValue[kSynthMacrosCount];

} SynthPartSettings;

//----------------------------------------------------------------------------------
struct strMtValues
{
    uint8_t lastUsedNote = 0;
    uint8_t lastUsedInstrument = 0;
    uint8_t lastUsedVolume = 0;
    uint8_t lastUsedFx = 0;

    uint8_t actualPattern = 1;

    uint8_t padBoardScale = 0;
    uint8_t padBoardNoteOffset = 11;
    uint8_t padBoardRootNote = 24;
    uint8_t padBoardMaxVoices = 8;

    uint8_t oldVolume = 0; // juz nie uzywana wartosc nowa jest w mtConfig

    uint8_t delayFeedback;
    uint16_t delayTime;
    uint8_t delayParams; // 7 - PINGPONG ENABLE , 6 - SYNC ENABLE, 5-0 SYNC RATE

    uint16_t limiterAttack = 100;
    float limiterRelease = 0.512;
    uint16_t limiterThreshold = 16384;

    uint8_t bitDepth = BIT_DEPTH_MAX;

    int8_t patternEditStep = 1;

    uint8_t trackMute[kSampleTracks] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    //********************************* radio
    uint8_t source;
    uint8_t gainLineIn;
    uint8_t gainMicLow;
    uint8_t gainMicHigh;
    uint8_t gainRadio;
    uint8_t monitor;
    float radioFreq = 87.5;

    uint8_t projectNotSavedFlag = 0; // 2 = never saved, 1 = base location Project not updated

    // performance 1/2
    uint16_t perfTracksPatterns[kSampleTracks] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    uint8_t perfFxPlaces[12] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
    }; // jakie efekty w 12 slotach
    int16_t perfFxValues[12][4] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 },
                                    { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };

    // song
    float globalTempo = DEFAULT_TEMPO;
    uint16_t patternLength = 32;

    uint8_t instrumentsToSave[kSampleInstrumentsCount];
    uint8_t patternsToSave[PATTERN_INDEX_MAX];

    // performance 2/2
    uint8_t perfSelectedValues[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t perfTracksState[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; // narazie nie uzywana

    uint8_t lineInVolume;
    uint8_t lineInMute = 1;
    uint8_t lineInDelaySend = 0;
    uint8_t lineInDelaySendMute = 1;
    uint8_t lineInReverbSend = 0;
    uint8_t lineInReverbSendMute = 1;

    uint8_t oldMidiInstrument[10] = { 0 };

    uint8_t allPatternsBitmask[PATTERN_INDEX_MAX];

    struct strReverbParams
    {
        float size;
        float damp;
        float predelay;
        float diffusion;
    } reverb;

    char TrackNames[8][21];

    uint8_t trackVolume[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
    uint8_t reverbVolume = 100;
    uint8_t delayVolume = 100;
    uint8_t reverbMute;
    uint8_t delayMute;
    uint8_t dryMixMute;
    uint8_t dryMixVolume = 100;

    float eqBandAdjustment[5];
    uint8_t bassBoost;
    uint8_t stereoBoost;

    uint8_t oldLineInMute;
    uint8_t oldLineInVolume;

    uint8_t patternDiv = 3; // czyli "co 4"
    uint8_t antialiasingEnable = 1;
    uint8_t limiterMode = limiterModeSubtle;
    uint8_t limiterSidechain = LIMITER_SIDECHAIN_DEFAULT;
    uint8_t sendsMode = sendsModePostFader;

    struct strMidiInstr
    {
        uint8_t channel; // na razie nieuzywane
        uint8_t velocity = DEFAULT_MIDI_VELOCITY;
        uint8_t ccOut[6] = {
            MIDI_CC_MODWHEEL,   MIDI_CC_GLIDE,   MIDI_CC_CHAN_VOLUME,
            MIDI_CC_EXPRESSION, MIDI_CC_SUSTAIN, MIDI_CC_SPECIAL_PC,
        };
        uint8_t offset = 0;
        uint8_t reserved[7] = { 0, 0, 0, 0, 0, 0, 0 };
    } midiInstrument[kMidiInstrumentsCount];

    uint16_t delayFilterFrequency;
    uint8_t isHeadroomEnabled;
    uint8_t saturationType;
    uint8_t isSnapToZeroEnabled;

    char midiTrackNames[kMaxMidiTracksInTrackers][TRACK_NAME_LENGTH + 1];
    uint8_t allPatternsBitmaskMidi[PATTERN_INDEX_MAX];
    uint8_t midiTrackMute[kMaxMidiTracksInTrackers] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    SynthPartSettings synthPart[kSynthInstrumentsCount];

    uint8_t recorderGainUsb;
    void setDefaultTrackName(int trackIdx);
};

//----------------------------------------------------------------------------------
struct strSong
{
    uint8_t playlist[SONG_MAX]{ 1 };
    uint8_t playlistPos = 0;
};

//----------------------------------------------------------------------------------
struct strMtProjectRemote
{
    strSong song;
    strMtValues values;
    char projectName[PROJECT_NAME_SIZE]; // used for identify source project name in workspace
    char projectPath[PATCH_SIZE - PROJECT_NAME_SIZE]; // and location of that project
};

//----------------------------------------------------------------------------------
struct strMtProject
{
    strInstrument instrument[kSampleInstrumentsCount];
    uint8_t instruments_count; // uzywane tylko do oblizania postepu w czasie ladowania
    uint8_t patterns_count;    // nie uzywane

    strSong song;
    strMtValues values;

    // dynamiczne
    strSampleMemory sampleMemory[kSampleInstrumentsCount];
    uint32_t max_memory = SAMPLE_MEMORY_SIZE;
    uint32_t used_memory;
    uint16_t perfTracksPatterns[kMaxAllTracksInTrackers] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };
};

//----------------------------------------------------------------------------------
enum NumberingMode
{
    kDecimalWithStepsFrom0,
    kDecimalWithStepsFrom1,
    kHexadecimal,

    kNumberingModeCount,
    kNumberingModeDefault = kDecimalWithStepsFrom1,
};

struct strMtConfig
{
    struct strConfigHeader
    {
        char id[7];
        uint8_t version;
        uint16_t totalSize;

    } header;

    struct strAudioCodecConfig
    {
        uint16_t size; // zawsze na poczatku wielkosc struktury

        uint8_t inSelect;
        uint8_t outSelect;

        float headphoneVolume;
        uint8_t inputGain; // 0-63
        uint8_t mutedHeadphone;
        uint8_t mutedLineOut;

        uint8_t lineInLeft;  // 0-15
        uint8_t lineInRight; // 0-15

        uint8_t lineOutLeft;  // 13-31
        uint8_t lineOutRight; // 13-31

        uint8_t volume;

    } audioCodecConfig;

    struct strVersion
    {
        uint16_t size; // zawsze na poczatku wielkosc struktury

        uint8_t ver_1;
        uint8_t ver_2;
        uint8_t ver_3;
        uint8_t beta; // 255 == official release
        uint8_t eepromStructVer;

    } firmware;

    struct strMIDIValues
    {
        uint16_t size; // zawsze na poczatku wielkosc struktury

        uint8_t clkIn;
        uint8_t clkOut;
        uint8_t transportIn;
        uint8_t transportOut;
        uint8_t notesInMode;
        uint8_t notesInChannel; // od 0 = all channels
        uint8_t notesOutMode;
        uint8_t ccInMode;
        uint8_t ccInChannel;
        uint8_t middleC;
        uint8_t slaveClockComp;
    } midi;

    struct strGlobalValues
    {
        uint16_t size; // zawsze na poczatku wielkosc struktury

        uint8_t padsLightBack = PADS_LIGHT_BACK_DEFAULT;
        uint8_t padsLightFront = PADS_LIGHT_FRONT_DEFAULT;
        uint8_t padsLightBackDim = PADS_LIGHT_BACK_DEFAULT / 2;

        uint16_t perfTracksPatterns[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        uint8_t perfFxPlaces[12] = {
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
        }; // jakie efekty w 12 slotach
        int16_t perfFxValues[12][4] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 },
                                        { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
        uint8_t perfSelectedValues[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        uint8_t perfTracksState[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; // narazie nie uzywana
    } values;

    struct strGeneralValues
    {
        uint16_t size; // zawsze na poczatku wielkosc struktury

        uint8_t radioRegion;
        uint8_t dispBrightness;
        uint8_t padsBrightness;
        uint8_t mtpState;

        uint8_t recOptions;
        uint8_t performanceSource;

        uint8_t numberingMode;
        uint8_t padPreviewInPatternEdit;

        uint8_t patternArrangement;
        uint8_t patternInfo;

        uint8_t lineInOutType;

        uint8_t theme;
        uint8_t font;
        uint8_t previewVolume;
        uint8_t recThreshold;
    } general;

    struct strInterfaceState
    {
        uint16_t size; // zawsze na poczatku wielkosc struktury

        uint8_t fxPopupDescription;

        uint8_t functionKeysBinding[8]; // mini fn buttons

    } interface;

    struct strMetronome
    {
        uint16_t size; // zawsze na poczatku wielkosc struktury

        uint8_t state;                    // 0-1
        uint8_t preRoll;                  // 0-1
        uint8_t timeSignatureNumerator;   // 0-11 - do zmiany pewnie
        uint8_t timeSignatureDenominator; // 0-11 - do zmiany pewnie
        uint8_t volume;                   // 0-100

    } metronome;

    struct strDebugState
    {
        uint16_t size; // zawsze na poczatku wielkosc struktury

        uint8_t debugLogState;

    } debug;

    // tu wszystko co nie ma jakiejsc szczegulnej kategori
    struct strCommonValues
    {
        uint16_t size; // zawsze na poczatku wielkosc struktury

        uint8_t equalSliceNumber = 8;

        uint8_t usbAudioMode = 0;

    } common;

    // /\ dodawac nowe wartosci do podstruktur powyżej na końcach każdej ze struktur
    // \/ jesli chcemy dodawac nowe podstruktury wymagana jest edycja mtConfig.cpp
};

//----------------------------------------------------------------------------------
struct strRecentFile
{
    struct ProjectRecord
    {
        char fullPath[PATCH_SIZE];
        static_assert(sizeof(fullPath) < kMaxFileMemoryPerRecentProject);
        char emptySpace[kMaxFileMemoryPerRecentProject - sizeof(fullPath)];
    } project[kRecentProjectListLength];
};

//-------------------------------------------------

extern strMtProject mtProject;
extern strMtConfig mtConfig;

struct strMtDispSpectrum
{
    uint8_t spectrumType = 0;
    int8_t upperData[480];
    int8_t lowerData[480];

    uint8_t pointsType = 0;
    int16_t startPoint;
    int16_t endPoint;
    int16_t loopPoint1;
    int16_t loopPoint2;
};

struct strMtDispValues
{
    uint8_t type[5];
    int16_t value1[5];
    //	int16_t value2[MT_DISP_VALUES_MAX];
    //	int16_t value3[MT_DISP_VALUES_MAX];
    //	int16_t value4[MT_DISP_VALUES_MAX];
    //	int16_t value5[MT_DISP_VALUES_MAX];
};

struct strMtDispMultiRowValues
{
    uint8_t type[5];
    char** labels[5];
    int16_t values[5][5];
};

struct strMtDispEnvelope
{
    uint8_t type;
    char* names[INSTRUMEN_ENVELOPES_MAX];

    uint8_t delay;
    uint8_t attack;
    uint8_t hold;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;

    uint8_t amount;
};

struct strMtDispTrackTable
{
    struct strStepParams
    {
        uint8_t mode;
        uint8_t iVal1;
        uint8_t iVal2;
        uint8_t iVal3;
        int8_t iVal4; // velocity
    } params[5];

    struct strStepFx
    {
        uint8_t mode;
        char* name;
        char* cVal1;
        char* cVal2;
        uint8_t iVal1;
        uint8_t iVal2;

    } fx1[5], fx2[5], fx3[5], fx4[5];

    uint8_t active[5]; // aktualne edytowana wartosc w kazdej kolumnie (params/fxs)
    uint8_t state[5];  // czy step lezy w sekwencji
};

struct strGridPad
{
    uint8_t note;
    int8_t microtune;
    bool ledEnable;
};

struct strGrid
{
    char name[GRID_NAME_SIZE] = { "New Grid" };
    strGridPad pad[48];
};

static constexpr int kC5Note = 60;

const char mtNotes[128][5] = {
    "C0",  "C#0", "D0",  "D#0", "E0",   "F0",  "F#0",  "G0",  "G#0", "A0",   "A#0", "B0",  "C1",
    "C#1", "D1",  "D#1", "E1",  "F1",   "F#1", "G1",   "G#1", "A1",  "A#1",  "B1",  "C2",  "C#2",
    "D2",  "D#2", "E2",  "F2",  "F#2",  "G2",  "G#2",  "A2",  "A#2", "B2",   "C3",  "C#3", "D3",
    "D#3", "E3",  "F3",  "F#3", "G3",   "G#3", "A3",   "A#3", "B3",  "C4",   "C#4", "D4",  "D#4",
    "E4",  "F4",  "F#4", "G4",  "G#4",  "A4",  "A#4",  "B4",  "C5",  "C#5",  "D5",  "D#5", "E5",
    "F5",  "F#5", "G5",  "G#5", "A5",   "A#5", "B5",   "C6",  "C#6", "D6",   "D#6", "E6",  "F6",
    "F#6", "G6",  "G#6", "A6",  "A#6",  "B6",  "C7",   "C#7", "D7",  "D#7",  "E7",  "F7",  "F#7",
    "G7",  "G#7", "A7",  "A#7", "B7",   "C8",  "C#8",  "D8",  "D#8", "E8",   "F8",  "F#8", "G8",
    "G#8", "A8",  "A#8", "B8",  "C9",   "C#9", "D9",   "D#9", "E9",  "F9",   "F#9", "G9",  "G#9",
    "A9",  "A#9", "B9",  "C10", "C#10", "D10", "D#10", "E10", "F10", "F#10", "G10"
};

const char mtVolumes[48][4] = {
    "0",  "3",   "5",   "8",   "11",  "14",  "16",  "19",  "22",  "24",  "27",  "30",
    "32", "35",  "38",  "41",  "43",  "46",  "49",  "51",  "54",  "57",  "59",  "62",
    "65", "68",  "70",  "73",  "76",  "78",  "81",  "84",  "86",  "89",  "92",  "95",
    "97", "100", "103", "105", "108", "111", "113", "116", "119", "122", "124", "127",

};

#endif
