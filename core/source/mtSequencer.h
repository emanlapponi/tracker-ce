#ifndef _MTSEQUENCER_H_
#define _MTSEQUENCER_H_

#include "Platform.h"
#include "internalTickClock.h"
#include "mtChordDefs.h"
#include "mtMidi.h"
#include "mtStructs.h"
#include "scales.h"
#include "util_math.h"
#include <stdint.h>

static void nullCallback(void){};

bool isSampleInstrument(int instrument);
bool isMidiInstrument(int instrument);
bool isSynthInstrument(int instrument);
int instrumentIdxToMidiChannel(int instrument);
int instrumentIdxToSynthIdx(int instrumentIdx);
bool isSampleTrack(int track);
bool isPerformanceTrack(int track);

static constexpr int kNoChannel = -1;

class Sequencer
{

  public:
    Sequencer();

    static constexpr int TRACKS = 8; // todo: use kAllTracks in futore or other in mtStructs
    typedef float seqtimesec_t;
    static constexpr float kUnknownExternalTempo = 0.0f;
    static constexpr seqtimesec_t kSecond = 1;
    static constexpr seqtimesec_t kMinute = 60 * kSecond;

    static constexpr int kStepFxSlots = 2;

    enum enStep
    {
        STEP_NOTE_EMPTY = -1,
        STEP_NOTE_OFF_FADE = -2,
        STEP_NOTE_OFF_CUT = -3,
        STEP_NOTE_OFF_DEFAULT = -4,
        STEP_NOTE_DEFAULT = 60,
        STEP_NOTE_MAX = 127,
        STEP_NOTE_MIN = 0,

        STEP_VELO_MIN = 0,
        STEP_VELO_MAX = 127,
        STEP_VELO_DEFAULT = -1,
        STEP_VELO_OFF = -2,
    };
    enum enFillStep
    {

        fillStepNote = 0,
        fillStepNoNote,
        fillStepFx,
        fillStepNoFx,
        fillStepRandom,
        fillStepByStep,
        fillStepEuclidean,
        fillStepEnd__
    };
    static constexpr int MAX_FILL_DENSITY = 128;

    enum enMisc
    {
        MINSTEP = 0,
        MAXSTEP = 127,
        DEFAULT_PATTERN_LENGTH = 32,

    };

    enum MuteState
    {
        kUnmuted,
        kMuted,
    };

    enum enPlaymode
    {
        PLAYMODE_MIN = 0,
        PLAYMODE_FORWARD = 0,
        PLAYMODE_BACKWARD,
        PLAYMODE_RANDOM,
        PLAYMODE_CUSTOM_1,
        PLAYMODE_CUSTOM_2,
        PLAYMODE_CUSTOM_3,
        PLAYMODE_CUSTOM_4,
        PLAYMODE_CUSTOM_5,
        PLAYMODE_CUSTOM_6,
        PLAYMODE_CUSTOM_7,
        PLAYMODE_CUSTOM_8,
        PLAYMODE_CUSTOM_9,
        PLAYMODE_CUSTOM_10,
        PLAYMODE_CUSTOM_11,
        PLAYMODE_CUSTOM_12,
        PLAYMODE_CUSTOM_13,
        PLAYMODE_CUSTOM_14,
        PLAYMODE_CUSTOM_15,
        PLAYMODE_CUSTOM_16,
        PLAYMODE_CUSTOM_17,
        PLAYMODE_CUSTOM_18,
        PLAYMODE_CUSTOM_19,
        PLAYMODE_CUSTOM_20,
        PLAYMODE_PINGPONG
    };

    enum enMidiInVoiceMode
    {
        midiInVoiceMode_sequencer,
        midiInVoiceMode_ignore,
        midiInVoiceMode_SamplePlayback,
        midiInVoiceMode_SampleEditor,
        midiInVoiceMode_SampleRecorder,
        midiInVoiceMode_default = midiInVoiceMode_sequencer,
    };

    static const float MAX_TEMPO;
    static const float MIN_TEMPO;

    static struct strFxConsts
    {
        enum enFxType
        {
            // powiązane z listą tekstów w
            //			 #include <interfaceDefs.h>
            FX_TYPE_NONE,
            FX_TYPE_OFF,
            FX_TYPE_MICROMOVE,
            FX_TYPE_ROLL,
            FX_TYPE_CHANCE,
            FX_TYPE_RANDOM_NOTE,
            FX_TYPE_RANDOM_INSTRUMENT,
            FX_TYPE_RANDOM_VOLUME,
            FX_TYPE_SEND_CC_A,
            FX_TYPE_SEND_CC_B,
            FX_TYPE_SEND_CC_C,
            FX_TYPE_SEND_CC_D,
            FX_TYPE_SEND_CC_E,
            FX_TYPE_BREAK_PATTERN, // dziala jak koniec patternu
            FX_TYPE_MIDI_CHORD,
            FX_TYPE_TEMPO,
            FX_TYPE_RANDOM_VALUE,

            FX_TYPE_SWING,
            FX_TYPE_VOLUME,
            FX_TYPE_GLIDE,
            FX_TYPE_GATE_LENGTH,
            FX_TYPE_ARP,
            FX_TYPE_POSITION,
            FX_TYPE_VOLUME_LFO,
            FX_TYPE_PANNING_LFO,
            FX_TYPE_SAMPLE_SLICE,
            FX_TYPE_REVERSE_PLAYBACK,
            FX_TYPE_FILTER_LOWPASS,
            FX_TYPE_FILTER_HIGHPASS,
            FX_TYPE_FILTER_BANDPASS,
            FX_TYPE_DELAY_SEND,
            FX_TYPE_PANNING,
            FX_TYPE_REVERB_SEND,
            FX_TYPE_FINETUNE_LFO,
            FX_TYPE_MICROTUNING,
            FX_TYPE_FILTER_LFO,
            FX_TYPE_POSITION_LFO,
            FX_TYPE_SEND_CC_F,
            FX_TYPE_OVERDRIVE,
            FX_TYPE_BIT_DEPTH,
            FX_TYPE_TUNE,
            FX_TYPE_SLIDE_UP,
            FX_TYPE_SLIDE_DOWN,
            FX_TYPE_R30,
            FX_TYPE_AMP_ENV = FX_TYPE_R30,
            FX_TYPE_R31,
            FX_TYPE_FILTER_ENV = FX_TYPE_R31,

            // FX_TYPE_Rx to pozostałość po starych efektach i zostają tu żeby nic się nie rozpadło
            FX_TYPE_R1,
            FX_TYPE_GRANULAR_LENGTH = FX_TYPE_R1,
            FX_TYPE_R2,
            FX_TYPE_R3,
            FX_TYPE_R7,
            FX_TYPE_R8,
            FX_TYPE_R9,

        };

        enum enRollPeriod
        {
            ROLL_PERIOD_NONE = 0,
            ROLL_PERIOD_MIN = 0,

            ROLL_PERIOD_1_1,
            ROLL_PERIOD_1_2,
            ROLL_PERIOD_1_3,
            ROLL_PERIOD_1_4,
            ROLL_PERIOD_1_6,
            ROLL_PERIOD_1_8,
            ROLL_PERIOD_1_12,
            ROLL_PERIOD_1_16,
            ROLL_PERIOD_MAX = ROLL_PERIOD_1_16
        };
        enum enStutterPeriod
        {
            STUTTER_PERIOD_NONE = 0,
            STUTTER_PERIOD_MIN = 0,
            STUTTER_PERIOD_16_1,
            STUTTER_PERIOD_12_1,
            STUTTER_PERIOD_8_1,
            STUTTER_PERIOD_6_1,
            STUTTER_PERIOD_4_1,
            STUTTER_PERIOD_3_1,
            STUTTER_PERIOD_2_1,
            STUTTER_PERIOD_1_1,
            STUTTER_PERIOD_1_2,
            STUTTER_PERIOD_1_3,
            STUTTER_PERIOD_1_4,
            STUTTER_PERIOD_1_6,
            STUTTER_PERIOD_1_8,
            STUTTER_PERIOD_1_12,
            STUTTER_PERIOD_1_16,
            STUTTER_PERIOD_MAX = STUTTER_PERIOD_1_16
        };

        enum enRollDir
        {
            rollType_const = 0,
            rollType_volDown,
            rollType_volUp,
            rollType_noteDown,
            rollType_noteUp,
            rollType_noteRandom,
            rollNoteDir_max = rollType_noteRandom,
        };

        enum enArpConsts
        {
            arpPeriod_none = -1,
            arpPeriod_8 = 0,
            arpPeriod_6,
            arpPeriod_4,
            arpPeriod_3,
            arpPeriod_2,
            arpPeriod_1,
            arpPeriod_1_2,
            arpPeriod_1_3,
            arpPeriod_1_4,
            arpPeriod_1_6,
            arpPeriod_1_8,

            arpPeriodsCount = 11,

            arpOption_up = 0,
            arpOption_down,
            arpOption_random,
        };

        enum enChanceConsts
        {
            CHANCE_PROB_MAX = 100,
            CHANCE_GROUP_WITH_LAST = 101,
            CHANCE_NOT_LAST = 102,
            CHANCE_PLAY_SKIP_START = 103,
            CHANCE_CYCLE_STEPS = 6,
            CHANCE_CYCLE_COUNT = CHANCE_CYCLE_STEPS * CHANCE_CYCLE_STEPS,
            CHANCE_PLAY_SKIP_END = CHANCE_PLAY_SKIP_START + CHANCE_CYCLE_COUNT - 1,
            CHANCE_SKIP_PLAY_START = CHANCE_PLAY_SKIP_END + 1,
            CHANCE_SKIP_PLAY_END = CHANCE_SKIP_PLAY_START + CHANCE_CYCLE_COUNT - 1,
            CHANCE_MAX = CHANCE_SKIP_PLAY_END,
        };

    } fx;

    enum enRollVolumeOption
    {
        rollVolumeOption_const = 0,
        rollVolumeOption_up,
        rollVolumeOption_down,
        rollVolumeOption_random,
    };

    struct strPattern
    {
        struct strFileHeader
        {
            char id_file[2];   // { 0 }; // typ pliku, np MT = plik projektu, TI = instrument
            uint16_t type;     // typ w liczbie
            char fwVersion[4]; // { 0 }; 		// wersja fw w ktorej zapisano plik
            char fileStructureVersion[4]; // { 0 };	// wersja struktury pliku
            uint16_t size;                // = 0;					// rozmiar
        } header;

        float tempoRezerwa; // = 100.0;
        float swingRez;     // = 50.0;

        uint8_t rezerwa1; // = 0;
        uint8_t rezerwa2; // = 0;
        uint8_t rezerwa3; // = 0;
        uint8_t rezerwa4; // = 0;

        struct strTrack
        {
            uint8_t length; // = DEFAULT_PATTERN_LENGTH - 1; // liczy od 0

            struct strStep
            {
                int8_t note;        // = STEP_NOTE_EMPTY;
                uint8_t instrument; // = 0;
                // FX
                struct strFx
                {
                    uint8_t type;  // = 0;
                    uint8_t value; // = 0;
                } fx[kStepFxSlots];
                int8_t volume;    // = STEP_VELO_DEFAULT (-1);
                int8_t micromove; // = -1;

                bool isNoteEmpty()
                {
                    return note == STEP_NOTE_EMPTY || note < STEP_NOTE_OFF_DEFAULT;
                }
                bool isNoteOff()
                {
                    return note == STEP_NOTE_OFF_FADE || note == STEP_NOTE_OFF_DEFAULT ||
                           note == STEP_NOTE_OFF_CUT;
                }
                bool hasAnyFx()
                {
                    return fx[0].type != 0 || fx[1].type != 0 || volume != STEP_VELO_DEFAULT || micromove >= 0;
                }

            } step[MAXSTEP + 1];

        } track[kAllTracks];

        uint32_t crc;
    };

    struct strSelection
    {
        uint8_t firstStep;  // = 0;
        uint8_t firstTrack; // = 0;
        uint8_t lastStep;   // = 0;
        uint8_t lastTrack;  // = 0;
    };

    // 16 tracks
    static_assert(sizeof(strPattern) == k16TracksPatternSize);

    struct strPlayer
    {
        bool songMode = 0;
        bool performanceMode = 0;
        bool selectionMode = 0;
        bool selectionModeLooped = 0;
        strSelection loopSelection;
        uint8_t selectionModeLastStep = 0;

        uint16_t sequencialSwitch_pattern = 0;
        bool sequencialSwitch_isArmed = 0;
        bool sequencialSwitch_loaded = 0;
        // Set by stop() in the audio callback, carried out by update().
        bool reloadPatternAfterStop = 0;
        int activePatternIdx = -1;

        bool isPlay = 0;
        bool isREC = 0;
        bool startRecOnExtClk = 0;
        bool isStop = 1;

        uint16_t uStep = 0;
        uint16_t globalPos = 0;

        bool breakPattern = 0; // stops pattern after current step
        bool endOfPattern = false;

        uint8_t midiInVoiceMode = midiInVoiceMode_sequencer;

        struct strPerformance
        {
            int16_t patternLength = -1;
            int16_t trackLength[kAllTracks];
        } performance;

        struct strTempoFx
        {
            bool isActive = 0;
            bool stopAfterCurrentStep = 0;

            float tempo = 0.0;

            void set(int16_t val)
            {
                isActive = 1;
                tempo = val;
                tempo = clamp(tempo, MIN_TEMPO_FX, MAX_TEMPO);
            }
            void reset()
            {
                isActive = 0;
                stopAfterCurrentStep = 0;
            }

        } tempoFx;

        struct strSwing
        {
            bool isActive = 0;
            bool stepToogle = 0;
            float value = 50.0f;

            void toogle() { stepToogle = !stepToogle; }
            float getTempoCorrection()
            {
                return stepToogle ? (100.0f - value) + 50.0f : value + 50.0f;
            }
            void setSwing(int16_t val)
            {
                isActive = 1;
                value = (float)clamp<int16_t>(val, 25, 75);
            }
            void resetSwing()
            {
                value = 50.0;
                isActive = 0;
                stepToogle = 0;
            }

        } swing;

        struct strPlayerTrack
        {
            struct strSendStep
            {
                int8_t note = STEP_NOTE_EMPTY;

                uint8_t instrument = 0;

                // FX
                struct strFx
                {
                    uint8_t type = 0;
                    uint8_t value = 0;

                } fx[kStepFxSlots];

                int8_t volume = -1; // jeśli <0 to nie wysyłamy
                int8_t micromove = -1;
            } stepSent, stepToSend, stepContRoll;

            bool isActive = 1;
            bool isRecordingArmed = 1;

            bool stepOpen = 0;
            bool noteOpen = 0;
            uint8_t recOpen = 0;
            uint8_t recNoteOnStepIdx = 0;

            int16_t noteSource = -1;

            uint16_t uStep = 0;              // aktualny microstep
            int16_t actual_pos = 0;          // aktualna pozycja w stepach
            int16_t actual_pos_absolute = 0; // aktualna pozycja na potrzeby custom playMode
            int16_t custom_fx = 0;
            int16_t custom_fx_value = 0;

            uint16_t stepTimer = 0;  // tu liczymy microstepy w górę
            uint16_t stepLength = 0; // z tym porównujemy timer
            uint16_t noteTimer = 0;
            int16_t noteLength = -1; // nieskonczenie dluga jesli -1

            uint8_t cancelStep = 0;
            uint32_t passCount = 0;
            bool lastCondFired = true;

            struct strRollFx
            {
                uint8_t isOn = 0;
                uint8_t period = 0; // 1/1, 1/2, 1/4 ...
                uint8_t val = 0;
                uint8_t type = 0;
                uint8_t fxSlot = 0;
                uint8_t volumeOption = rollVolumeOption_const;
                int8_t startingVolume = -1;
                uint16_t length = 1; // obliczona długość na potrzeby vol up i down

                void reset()
                {
                    isOn = 0;
                    period = fx.ROLL_PERIOD_NONE;
                }
            } roll;

            struct strArpFx
            {
                uint8_t isOn = 0;
                int8_t period = -1;
                uint8_t option = 0;

                uint16_t timer = 0;
                uint16_t length = 0;

                uint8_t chord = 0;
                uint8_t baseNote = 36;

                // kontynuacja od 1,
                // zakladamy ze 0 poszlo jako pierwsze
                uint8_t nextPosInBitmask = 0;

                void start()
                {
                    isOn = 1;
                    timer = 0;

                    switch (option)
                    {
                        case fx.arpOption_down: nextPosInBitmask = 11; break;

                        default: nextPosInBitmask = 0; break;
                    }
                }
                void reset()
                {
                    isOn = 0;
                    period = -1;
                    option = 0;

                    timer = 0;
                }
            } arp;

            struct strTuneMidiFx
            {
                bool isActive = 0;
                int8_t tune = 0;

            } tuneMidi;

            struct strPerformanceMidiVolumeFx
            {
                static constexpr int minValue = -100;
                static constexpr int maxValue = 100;
                int value = 0;

            } performanceMidiVolumeFx;

            int8_t performanceStepRepeater = 0;
            int8_t performancePlayMode = 0;
            int performanceSourcePattern = -1;

            struct strMicroMoveFx
            {
                uint8_t isActive = 0;
                uint8_t isArmed = 0;
                uint16_t offsetValue = 0;
                uint16_t timer = 0;

                strSendStep stepToMove;

                void set(uint16_t val)
                {
                    offsetValue = val;
                    offsetValue = clamp<uint16_t>(val, 0, 2 * 48);
                    isActive = 1;
                    isArmed = 0;
                    timer = 0;
                }
                void reset()
                {
                    offsetValue = 0;
                    isActive = 0;
                    isArmed = 0;
                    timer = 0;
                }
                void arm(strSendStep stepToBuffer)
                {
                    if (isActive && !isArmed)
                    {
                        stepToMove = stepToBuffer;
                        isArmed = 1;
                    }
                }
                bool isMicromoveReady()
                {
                    if (isArmed && timer == offsetValue)
                        return true;
                    else
                        return false;
                }
            } micromove;

        } track[kAllTracks];

        void (*onPatternOrSelectionEnd)(void) = nullCallback;
        void (*onSongEnd)(void) = nullCallback;
        void (*onGlobalStep)(void) = nullCallback;

        int extRecMetronomeStep = 0;
        uint8_t preRecMetronomeActive = 0;
        void (*preRollMetronomeEndCalback)(void) = nullptr;
    };

    enum enSelectionElements
    {
        ELEMENTS_ALL_NO_PREFERENCES,
        ELEMENTS_ALL_WITH_PREFERENCES,
        ELEMENTS_NOTES,
        ELEMENTS_INSTRUMENTS,
        ELEMENTS_FX1,
        ELEMENTS_FX2,
        ELEMENTS_MICROMOVE,
        ELEMENTS_VOLUME,
    };
    enum enAltMidiChannel
    {
        GRID_OUTSIDE_PATTERN = 20,
        GRID_INSIDE_PATTERN = 21,
        GRID_INSIDE_PATTERN_INSTRUMENT = 22,
    };

    struct strSelection selection, copySelection, pasteSelection;

    void init();
    void update();
    void setCrcCalculator(CrcCalculator* crcCalculator);
    void initAfterReadConfig();
    void tick(float intervalSeconds);

    bool isPlay(void);
    bool isRec(void);

    uint8_t isRecArmedOnTrack(uint8_t track);
    void toogleRecArmedOnTrack(uint8_t track);
    int8_t getSingleArmedTrack();

    uint8_t isStop(void);
    bool isPlayingSelection(void);
    bool isPlayingSelectionLooped(void);
    bool isLoopSelectionSingleRow(void);
    uint8_t isRenderMode(void);

    float getCurrentTempo();
    float getExternalTempo() { return externalTempo; }
    uint32_t getSeqTimer();

    strPlayer const* ptrPlayer = &player;

    strPattern* getPatternToSaveToFile() { return currentPattern; }

    strPattern* getActualPattern() { return currentPattern; }
    strPattern* getBuffPattern() { return bufferPattern; }

    bool isPerformanceTrackChange(uint8_t track)
    {
        return player.track[track].performanceSourcePattern >= 0;
    }

    void saveToFileDone() {}

    void enterPerformanceMode();

    enum enSeqState
    {
        SEQ_STATE_STOP = 0,
        SEQ_STATE_PLAY_PATTERN,
        SEQ_STATE_PLAY_SONG,
        SEQ_STATE_PLAY_PERFORMANCE,
        SEQ_STATE_PLAY_SELECTION,
    };

    enSeqState getSeqState()
    {
        if (player.isStop)
            return SEQ_STATE_STOP;
        if (player.performanceMode)
            return SEQ_STATE_PLAY_PERFORMANCE;
        if (player.isPlay && player.selectionMode)
            return SEQ_STATE_PLAY_SELECTION;
        if (player.isPlay && !player.songMode)
            return SEQ_STATE_PLAY_PATTERN;
        if (player.isPlay && player.songMode)
            return SEQ_STATE_PLAY_SONG;

        return SEQ_STATE_STOP;
    }

    uint8_t getPatternLengthWithSel()
    {
        if (player.performanceMode)
        {
            if (player.performance.patternLength > -1)
            {
                return player.performance.patternLength;
            }
            else
            {
                return player.performance.trackLength[0];
            }
        }
        else if (player.selectionMode)
        {
            return player.selectionModeLastStep;
        }

        return currentPattern->track[0].length;
    }

    uint8_t isSongMode() { return player.songMode == 1; }

    bool isPerformanceMode() { return player.performanceMode; }

    void exitPerformanceMode()
    {
        player.performanceMode = 0;
        player.performance.patternLength = -1;
    }

    bool isPerformancePatternChanged()
    {
        if (!player.performanceMode)
            return false;

        for (uint8_t track = 0; track < kPerformanceTracks; track++)
        {
            if (mtProject.perfTracksPatterns[track] != mtProject.values.actualPattern)
            {
                return true;
            }
        }

        return false;
    }

    strPattern* getPatternToLoadFromFile() { return bufferPattern; }
    void clearAllPatternSlots();

    strPattern* getPatternToUI() { return currentPattern; }

    bool copyBufferToBufferPattern(uint8_t* srcBuff);
    bool copyCurrentPatternToBuff(uint8_t* srcBuff);

    void switchRamPatternsNow();
    void switchPerformanceTrackNow(uint8_t trackToSwitch);
    void setTrackToLoadOnSwitch(uint8_t track, uint8_t sourcePattern);
    void setTrackToLoadNow(uint8_t track, uint8_t sourcePattern);
    void setPerformanceTrackLength(uint8_t track, uint8_t length);

    void setSynthMute(MuteState muteState);

    void sequencialSwitch_changeNextPattern(int8_t delta);
    void sequencialSwitch_SetReady();
    void sequencialSwitch_SetReadyNow();
    void sequencialSwitch_Reset();
    uint16_t sequencialSwitch_GetNext();

    void playPattern();
    void playPattern(uint8_t fromPos);
    void playSelection(bool looped = false);
    float getPlaySelectionProgress(void);
    void playSong(void);
    void playSongFromPosition(uint8_t songPos);
    void recStart(void);
    void showPreRollPopup();
    bool startRecOnExtClk_Active(void);
    void startRecOnExtClk_Reset(void);
    void recEnable(bool enable);
    void stop(void);
    void stopManualNotes(void);
    void continueSong(uint8_t songPos);
    void loopCurrentPattern(uint8_t songPos);
    int16_t findSlotWithPattern();
    uint8_t isMetronomeActive();
    bool isMetronomeApplicableForActualRecordingSource(void);
    uint8_t isPreRollConfigOn();
    uint8_t getMetronomeNumerator();
    uint8_t getMetronomeDenominator();
    void setMidiInVoiceMode(enMidiInVoiceMode mode);
    uint8_t getMidiInVoiceMode();

    float getStepPeriodMs(void);
    float getTimerPeriod_us(void);
    void attachToTimer(void);
    void detachFromTimer(void);

    // SELECTION
    void insert(strSelection* selection);
    void insertReversed(strSelection* selection);
    bool backspace();

    void copySelectionToBuffer(strSelection* from, strSelection* to);
    void pasteSelectionFromBuffer(strSelection* from, strSelection* to, uint8_t elements);
    void copyToBuffer();
    void pasteFromBuffer(uint8_t);
    void copyStep(strPattern::strTrack::strStep* srcStep,
                  strPattern::strTrack::strStep* destStep,
                  bool toMidi,
                  int elements = ELEMENTS_ALL_NO_PREFERENCES);
    bool isSelectionCorrect(strSelection* selection);
    bool isSingleSelection(strSelection* selection);

    void setSelection(uint8_t stepFrom, uint8_t trackFrom, uint8_t stepTo, uint8_t trackTo);
    void setCopySelection(uint8_t stepFrom, uint8_t trackFrom, uint8_t stepTo, uint8_t trackTo);
    void setPasteSelection(uint8_t stepFrom, uint8_t trackFrom, uint8_t stepTo, uint8_t trackTo);
    uint8_t getCopySelectionHeight();
    int8_t getSelectionFirstNote();

    void clearSingleTrack(strPattern::strTrack* track);
    void clearPattern(strPattern* patt);
    void setPatternHeader(strPattern* patt);
    void clearStep(strPattern::strTrack::strStep*);
    void clearStep(strPattern::strTrack::strStep* step, uint8_t);
    void clearSelected(strSelection* sel, uint8_t elements);
    void clearSelected();
    void clearSelected(uint8_t);
    void clearCopyPasteBuffer();

    void changeSelectionFxValue(uint8_t index, int16_t value);
    void changeSelectionFxType(uint8_t index, int16_t value);
    void changeSelectionNote(int16_t value);
    void changeSelectionNote(int16_t value, bool octaveShift);
    void changeSelectionInstrument(int16_t value);
    uint8_t changeInstrumentInSpecificOrder(int8_t actualValue, int16_t delta, bool midiTypeOnly);
    uint8_t getInstrumentVelo(uint8_t);
    uint8_t getInstrumentVolume(uint8_t);
    uint8_t getInstrumentMidiOffset(uint8_t instrument);

    void setSelectionFxType(uint8_t, int16_t);
    void setSelectionFxValueByPad(uint8_t index, int16_t value);
    void changeSelectionMicromove(int16_t value);
    void setSelectionMicromoveByPad(int16_t pad);
    void changeSelectionVolume(int16_t value);
    void setSelectionVolumeByPad(int16_t pad);
    void setSelectionInstrument(int16_t value);
    void setSelectionNote(int16_t value);

    void setStepRepeater(uint8_t track, int8_t stepRepeat);
    void setPerformancePlayMode(uint8_t track, int8_t playmode);

    void fillRandomNotes(int16_t, int16_t, int16_t, int16_t, int16_t);
    void fillLinearNotes(int16_t, int16_t, int16_t, int16_t, int16_t);
    void fillRandomInstruments(int16_t step, int16_t from, int16_t to, int16_t density);
    void fillLinearInstruments(int16_t step, int16_t from, int16_t to, int16_t density);
    void fillLinearFx(int16_t index,
                      int16_t fillStep,
                      int16_t fxType,
                      int16_t fromVal,
                      int16_t toVal,
                      int16_t density);
    void fillRandomFx(int16_t index,
                      int16_t fillStep,
                      int16_t fxType,
                      int16_t fromVal,
                      int16_t toVal,
                      int16_t density);
    void fillLinearMicromove(int16_t fillStep, int16_t fromVal, int16_t toVal, int16_t density);
    void fillRandomMicromove(int16_t fillStep, int16_t fromVal, int16_t toVal, int16_t density);
    void fillLinearVolume(int16_t fillStep, int16_t fromVal, int16_t toVal, int16_t density);
    void fillRandomVolume(int16_t fillStep, int16_t fromVal, int16_t toVal, int16_t density);

    bool isEuclideanStep(uint8_t offset, int16_t density);

    bool isStepToFillNote(strPattern::strTrack::strStep* step,
                          uint8_t offset,
                          int16_t fillStep,
                          int16_t density);
    bool isStepToFillFx(strPattern::strTrack::strStep* step,
                        uint8_t offset,
                        uint8_t fxIndex,
                        int16_t fillStep,
                        int16_t density);
    bool isStepToFillProp(strPattern::strTrack::strStep* step,
                          uint8_t offset,
                          bool isPropSet,
                          int16_t fillStep,
                          int16_t density);

    uint8_t generateRandomFx();
    void invertSelectedSteps(uint8_t);
    bool duplicatePattern();
    bool expandPattern();
    bool shrinkPattern();

    void setOnPatternOrSelectionEnd(void (*action)(void))
    {
        player.onPatternOrSelectionEnd = action;
    }
    void setOnSongEnd(void (*action)(void)) { player.onSongEnd = action; }
    void setOnGlobalStep(void (*action)(void)) { player.onGlobalStep = action; }

    void setPerformanceMidiTune(uint8_t tr, int8_t value);
    void setPerformanceMidiVolume(int track, int value);

    void setPerformancePatternLength(int length);
    void setPerformancePatternLengthFromFxVal(int8_t val);

    bool isSetInternalClock() { return (mtConfig.midi.clkIn == clockIn_Internal); }
    bool isFirstPatternStep(void);
    bool isLastPatternStep(void);

    void handleNoteOn(byte channel, byte pitch, int8_t volume, int16_t source);
    void handleNoteOff(byte channel, byte pitch, byte volume, int16_t source);
    void updateTrackLengths(strPattern* pattern);

    uint8_t noMoreRecOpen();

    int16_t getFxMax(uint8_t fxID, bool forRandom = false);
    int16_t getFxMin(uint8_t fxID, bool forRandom = false);
    int16_t getFxDefault(uint8_t fxID);
    int16_t getFxValueToView(uint8_t fxID, uint8_t track, uint8_t step);
    int16_t getFxValueCorrection(uint8_t type, uint8_t value);
    char getRollTypeChar(uint8_t val);
    uint8_t calcStepLength(uint8_t track, uint8_t step);
    int16_t rollValueToPeriod(int16_t value);
    void makeFxValLabelForSeqStep(char* ptr, int len, uint8_t fxID, uint8_t track, uint8_t step);
    enum enFxValLabelAlign
    {
        fxValLabelAlign_right = 0,
        fxValLabelAlign_center,
    };
    void makeFxValLabelWithAlgin(char* ptr,
                                 int len,
                                 uint8_t fxType,
                                 uint8_t value,
                                 enFxValLabelAlign align);

    uint8_t getActualPos() { return player.track[0].actual_pos; }
    uint8_t getPatternLength() { return getActualPattern()->track[0].length + 1; }

    void alignToGlobalPos(uint8_t);

    void setExternalClockComp(uint8_t configValue);

    void handleMidiInClock();
    void handleMidiInStart(bool continuePlay = false, bool songMode = false);
    void handleMidiInStop();
    void handleMidiInSongPosition(uint16_t beats);

    void setPreRollMetronomeEndCallback(void (*preRollMetronomeEndCalback)(void))
    {
        player.preRollMetronomeEndCalback = preRollMetronomeEndCalback;
    };

  private:
    static constexpr int kTicksInStep = 576;
    static constexpr size_t kExternalTempoCounterSize = 24 * 2;
    static constexpr int kExternalClockPpqn = 24;
    static constexpr int kStepsPerBeat = 4;

    static constexpr int kuStepsInStep = 48;
    static constexpr int kNanoStepsInUStep = 12;

    // efekty to bajty 0-255; żeby zmieścić >250 BPM liczone są co dwa.
    // np. wartość 5 to 10 BPM, wartość 200 to 400 BPM. Wartość 4 jest specjalna,
    // to TSTP.
    static const float TEMPO_FX_STP;
    static const float MIN_TEMPO_FX;
    static const float MAX_TEMPO_FX;

    void play(void);
    void play(uint8_t fromPos);

    void tick_uStep(void);

    void applyRowCustomFx(uint8_t x, uint8_t orderTemp);

    uint8_t isCustomPlayMode(uint8_t row);

    uint32_t timerTicks = 0;

    void switchRowStep(uint8_t row);
    void reset_actual_pos(void);
    void reset_actual_pos(uint8_t row, uint8_t delta = 0);
    void resetChanceState(void);
    bool evaluateChanceFx(uint8_t row, uint8_t value);

    void play_microStep(int row);
    void inc_uStep(uint8_t row);
    void inc_uStepGlobal();

    void loadNextPattern(uint8_t patternNumber);

    uint8_t isTrackEngineMuted(uint8_t track);

    void stopAllOpenNotes(void);
    void stopOpenNoteOnTrack(int track, bool leaveStepOpen = false);

    void cancelFxes(int8_t track);
    void cancelFxes();
    void killFxOnSlot(uint8_t row, uint8_t slot);
    uint8_t getRollType(uint8_t value);
    uint8_t getRollVelo(uint8_t, const strPlayer::strPlayerTrack::strSendStep* const sendStep);

    uint8_t rollValToVolumeOption(uint8_t);
    int8_t rollValToPeriod(int8_t rollVal);
    inline uint16_t rollValToMicroSteps(int8_t rollType);
    inline uint16_t stutterValToPeriod(int8_t rollType);

    uint16_t arpPeriodToMicrosteps(int8_t period);

    void midiSendCC(uint8_t channel, uint8_t control, uint8_t value, uint8_t midiOut);

    void sendNoteOn(uint8_t track, strPlayer::strPlayerTrack::strSendStep* step);
    void sendNoteOff(uint8_t track,
                     strPlayer::strPlayerTrack::strSendStep* step,
                     int8_t offOption = STEP_NOTE_OFF_DEFAULT);

    void sendMidiChordOn(uint8_t track, strPlayer::strPlayerTrack::strSendStep* step);
    void sendMidiChordOff(uint8_t track, strPlayer::strPlayerTrack::strSendStep* step);

    uint16_t getChordBitmask(uint8_t chord);
    uint16_t getChordBitmaskForArp(uint8_t chord);

    void send_clock(uint8_t);
    void stopAllMidiOpenNotes(void);

    int8_t velocityToVolume(int8_t velo);
    int8_t volumeToVelocity(int8_t vol);

    uint8_t recMicrotiming();
    uint8_t recVelocity();
    uint8_t getDestRecordingStep(uint8_t track, bool quantize, bool noteOff = false);

    uint8_t getNextArpNote(strPlayer::strPlayerTrack* playerRow);

    void updateExternalClockTempo(seqtimesec_t deltaTime);

    void updatePatternManagment();

    // The audio callback must not use the SD card, so stop() needs to know
    // whether it may reload the pattern itself.
    enum class StopContext
    {
        mainLoop,
        audioCallback,
    };
    void stop(StopContext context);
    void reloadPatternAfterStop();
    void stopForMissingNextPattern();

    strPattern* currentPattern;
    strPattern* bufferPattern;

    strPlayer player;

    int externalClockPulses = 0;
    int externalClocksToPlay = 0;

    InternalTickClock internalTickClock;

    seqtimesec_t timeWithoutClockPulse = 0;
    int16_t externalClockCompensation_clockSignals = 0;
    int16_t clockSignalsToCompensate = 0;

    seqtimesec_t externalClockEvents[kExternalTempoCounterSize]{};
    uint8_t externalClockEventsCount = 0;
    bool externalClockIntervalValid = false;
    seqtimesec_t externalClockEventTime = 0;

    uint8_t lastStoppedStepPos = 0;
    uint16_t pendingSongPositionBeats = 0;
    bool hasPendingSongPosition = false;

    float externalTempo = DEFAULT_TEMPO;
    float lastKnownExternalTempo = DEFAULT_TEMPO;

    uint8_t renderMode = 0;

    MuteState synthsMuteState = MuteState::kUnmuted;

    CrcCalculator* crcCalculator;
    struct HeldNoteSource
    {
        int16_t source;
        uint8_t note;
    };
    static constexpr uint8_t kMaxLiveRecHeldNotes = 32;
    HeldNoteSource liveRecHeldNotes[kMaxLiveRecHeldNotes];
    uint8_t liveRecHeldNotesCount = 0;
};

extern Sequencer sequencer;

void cbkSequencialSwitch_SetReady(void);

#endif
