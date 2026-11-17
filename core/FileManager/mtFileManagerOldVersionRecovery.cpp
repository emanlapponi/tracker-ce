#include "mtFileManagerOldVersionRecovery.h"
#include "TrackerImpl.h"
#include "convertOldFileVersionVolume.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "log.h"
#include "mtAudioEngine.h"

#if FILEMANAGER_HANDLE_SYNTH == 1
#include "Synth.h"
#endif

static void recoveryProjectVersion1();
static void recoveryProjectVersion2();
static void recoveryProjectVersion3();
static void recoveryProjectVersion4();
static void recoveryProjectVersion5();
static void recoveryProjectVersion6();
static void recoveryProjectVersion7();
static void recoveryProjectVersion8();
static void recoveryProjectVersion9();
static void recoveryProjectVersion10();
static void recoveryProjectVersion11();
static void recoveryProjectVersion12();
static void recoveryProjectVersion13();
static void recoveryProjectVersion14();
static void recoveryProjectVersion15();
static void recoveryProjectVersion16();
static void recoveryInstrumentVersion1();
static void recoveryInstrumentVersion2();
static void recoveryInstrumentVersion3();
static void recoveryInstrumentVersion4();
static void recoveryInstrumentVersion5();
static void recoveryInstrumentVersion6();
static void recoveryInstrumentVersion7();
static void recoveryInstrumentVersion8();

mtVersionRecovery versionRecovery;

//***************************************************************PROJECT
// przepisanie funkcji do wskaznika
mtVersionRecovery::mtVersionRecovery()
{
    // PROJECT
    recoveryProjectFunction[0] = recoveryProjectVersion1;
    recoveryProjectFunction[1] = recoveryProjectVersion2;
    recoveryProjectFunction[2] = recoveryProjectVersion3;
    recoveryProjectFunction[3] = recoveryProjectVersion4;
    recoveryProjectFunction[4] = recoveryProjectVersion5;
    recoveryProjectFunction[5] = recoveryProjectVersion6;
    recoveryProjectFunction[6] = recoveryProjectVersion7;
    recoveryProjectFunction[7] = recoveryProjectVersion8;
    recoveryProjectFunction[8] = recoveryProjectVersion9;
    recoveryProjectFunction[9] = recoveryProjectVersion10;
    recoveryProjectFunction[10] = recoveryProjectVersion11;
    recoveryProjectFunction[11] = recoveryProjectVersion12;
    recoveryProjectFunction[12] = recoveryProjectVersion13;
    recoveryProjectFunction[13] = recoveryProjectVersion14;
    recoveryProjectFunction[14] = recoveryProjectVersion15;
    recoveryProjectFunction[15] = recoveryProjectVersion16;

    // INSTRUMENT
    recoveryInstrumentFunction[0] = recoveryInstrumentVersion1;
    recoveryInstrumentFunction[1] = recoveryInstrumentVersion2;
    recoveryInstrumentFunction[2] = recoveryInstrumentVersion3;
    recoveryInstrumentFunction[3] = recoveryInstrumentVersion4;
    recoveryInstrumentFunction[4] = recoveryInstrumentVersion5;
    recoveryInstrumentFunction[5] = recoveryInstrumentVersion6;
    recoveryInstrumentFunction[6] = recoveryInstrumentVersion7;
    recoveryInstrumentFunction[7] = recoveryInstrumentVersion8;
}
// zwraca wskaznik do aktywnego odczytanego projektu
strMtProjectRemote* mtVersionRecovery::getReadedProject()
{
    return receivedProject;
}

// update project name only in last version
static int projectNameShift = 0;

// wykonuje serie translacji od odczytanej wersji do najnowszej
void mtVersionRecovery::translateProject(uint8_t firstVersion,
                                         uint8_t lastVersion,
                                         strMtProjectRemote* currentProject)
{
    receivedProject = currentProject;
    projectNameShift = 0;

    for (uint8_t i = firstVersion; i < lastVersion; i++)
    {
        recoveryProjectFunction[i - 1]();
    }
}

// odzyskiwanie danych z projektu Versja 1
static void recoveryProjectVersion1()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();
    strMtProjectRemote tmp;

    memcpy(&tmp, receivedProject, sizeof(tmp));

    uint32_t bytesShift =
      (uint32_t)&receivedProject->values.limiterAttack - (uint32_t)&receivedProject->values;
    uint32_t bytesToCopy = sizeof(strMtValues) - bytesShift - 1;

    memcpy((uint8_t*)&receivedProject->values.limiterAttack,
           (uint8_t*)(((uint32_t)&tmp.values.limiterAttack) - 4),
           bytesToCopy);
    receivedProject->values.delayFeedback = 50;
    receivedProject->values.delayTime = 500;
    receivedProject->values.delayParams = 0;

    projectNameShift += 4;
    // shift 4 zostal dobrany metoda prob i bledow, teoretycznie dodano 1 byte ale prawdopdobnie
    // kompilator to ulozyl w jakis inny sposob
}
// odzyskiwanie danych z projektu Versja 2
static void recoveryProjectVersion2()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    receivedProject->values.reverb.size = DEFAULT_REVERB_SIZE;
    receivedProject->values.reverb.damp = DEFAULT_REVERB_DAMP;
    receivedProject->values.reverb.predelay = DEFAULT_REVERB_PREDELAY;
    receivedProject->values.reverb.diffusion = DEFAULT_REVERB_DIFFUSION;

    projectNameShift += sizeof(strMtValues::strReverbParams);
}

// odzyskiwanie danych z projektu Versja 3
static void recoveryProjectVersion3()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    for (uint8_t track = 0; track < 8; track++)
    {
        sprintf(&receivedProject->values.TrackNames[track][0], "Track %d", track + 1);
    }

    projectNameShift += (8 * 21);
}

static void recoveryProjectVersion4()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    for (uint8_t track = 0; track < 8; track++)
    {
        receivedProject->values.trackVolume[track] = 100;
    }
    receivedProject->values.reverbVolume = 100;
    receivedProject->values.delayVolume = 100;

    projectNameShift += (10 * sizeof(uint8_t));
}
static void recoveryProjectVersion5()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();
    receivedProject->values.reverbMute = 0;
    receivedProject->values.delayMute = 0;
    receivedProject->values.dryMixMute = 0;
    receivedProject->values.dryMixVolume = 100;

    projectNameShift += (4 * sizeof(uint8_t));
}

static void recoveryProjectVersion6()
{
    ConvertOldFileVersionVolume volumeConverter;

    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    for (uint8_t track = 0; track < 8; track++)
    {
        receivedProject->values.trackVolume[track] =
          volumeConverter.convertMixerVolumeVersion1117to1118(
            receivedProject->values.trackVolume[track]);
    }

    receivedProject->values.dryMixVolume =
      volumeConverter.convertMixerVolumeVersion1117to1118(receivedProject->values.dryMixVolume);
    receivedProject->values.reverbVolume =
      volumeConverter.convertMixerVolumeVersion1117to1118(receivedProject->values.reverbVolume);
    receivedProject->values.delayVolume =
      volumeConverter.convertMixerVolumeVersion1117to1118(receivedProject->values.delayVolume);
}

static void recoveryProjectVersion7()
{
    ConvertOldFileVersionVolume volumeConverter;
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    receivedProject->values.limiterThreshold =
      volumeConverter.convertLimiterThresholdVersion110to112(
        receivedProject->values.limiterThreshold);

    for (uint8_t band = 0; band < 5; band++)
    {
        receivedProject->values.eqBandAdjustment[band] = 0.0f;
    }

    receivedProject->values.bassBoost = 0;
    receivedProject->values.stereoBoost = 0;

    projectNameShift += (5 * sizeof(float) + 2 * sizeof(uint8_t));
}

static void recoveryProjectVersion8()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    receivedProject->values.lineInMute = 1;
    receivedProject->values.lineInVolume = DEFAULT_LINE_IN_VOLUME;

    projectNameShift += (2 * sizeof(uint8_t));
}

// do wersji 1.3.0b3
static void recoveryProjectVersion9()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    strMtValues* v = &receivedProject->values;
    v->limiterSidechain = LIMITER_SIDECHAIN_DEFAULT;
    v->patternDiv = 3; // czyli 4
    v->padBoardScale = 0;
    v->padBoardNoteOffset = 11;
    v->padBoardRootNote = 24;
    v->antialiasingEnable = 1;
    v->limiterMode = limiterModeSubtle;
    v->sendsMode = sendsModePostFader;

    // 5 bajtow, padBoard* juz byly wczesniej.
    projectNameShift += (5 * sizeof(uint8_t));
}

// do wersji 1.3.0b4
static void recoveryProjectVersion10()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    strMtValues* v = &receivedProject->values;
    for (uint8_t i = 0; i < 16; i++)
    {
        v->midiInstrument[i].velocity = 127;
        v->midiInstrument[i].channel = i;
        v->midiInstrument[i].offset = 0;
        v->midiInstrument[i].ccOut[0] = MIDI_CC_MODWHEEL;
        v->midiInstrument[i].ccOut[1] = MIDI_CC_GLIDE;
        v->midiInstrument[i].ccOut[2] = MIDI_CC_CHAN_VOLUME;
        v->midiInstrument[i].ccOut[3] = MIDI_CC_EXPRESSION;
        v->midiInstrument[i].ccOut[4] = MIDI_CC_SUSTAIN;
        v->midiInstrument[i].ccOut[5] = MIDI_CC_SPECIAL_PC;
        for (uint8_t j = 0; j < 7; j++)
        {
            v->midiInstrument[i].reserved[j] = 0;
        }
    }

    v->midiInstrument[0].velocity = v->lineInVolume;
    v->midiInstrument[1].velocity = v->lineInMute;
    v->midiInstrument[2].velocity = v->lineInDelaySend;
    v->midiInstrument[3].velocity = v->lineInDelaySendMute;
    v->midiInstrument[4].velocity = v->lineInReverbSend;
    v->midiInstrument[5].velocity = v->lineInReverbSendMute;
    for (uint8_t i = 6; i < 16; i++)
    {
        v->midiInstrument[i].velocity = v->oldMidiInstrument[i - 6];
    }

    projectNameShift += sizeof(strMtValues::strMidiInstr);
}

// do wersji 1.4.0b1
static void recoveryProjectVersion11()
{
    ConvertOldFileVersionVolume volumeConverter;

    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    for (uint8_t track = 0; track < 8; track++)
    {
        receivedProject->values.trackVolume[track] =
          volumeConverter.convertMixerVolumeVersion1300to1302(
            receivedProject->values.trackVolume[track]);
    }

    receivedProject->values.dryMixVolume =
      volumeConverter.convertMixerVolumeVersion1300to1302(receivedProject->values.dryMixVolume);
    receivedProject->values.reverbVolume =
      volumeConverter.convertMixerVolumeVersion1300to1302(receivedProject->values.reverbVolume);
    receivedProject->values.delayVolume =
      volumeConverter.convertMixerVolumeVersion1300to1302(receivedProject->values.delayVolume);
    receivedProject->values.lineInVolume =
      volumeConverter.convertMixerVolumeVersion1300to1302(receivedProject->values.lineInVolume);
}

// do wersji 1.4.0b2
static void recoveryProjectVersion12()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();

    if (receivedProject->values.lineInDelaySend > 100)
    {
        receivedProject->values.lineInDelaySend = 0;
        receivedProject->values.lineInDelaySendMute = mtAudioEngineConstans::MUTE_DISABLE;
    }

    if (receivedProject->values.lineInReverbSend > 100)
    {
        receivedProject->values.lineInReverbSend = 0;
        receivedProject->values.lineInReverbSendMute = mtAudioEngineConstans::MUTE_DISABLE;
    }

    receivedProject->values.lineInMute = receivedProject->values.oldLineInMute;
    receivedProject->values.lineInVolume = receivedProject->values.oldLineInVolume;
    receivedProject->values.oldLineInMute = 0;
    receivedProject->values.oldLineInVolume = 0;
}

static void recoveryProjectVersion13()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();
    receivedProject->values.delayFilterFrequency = 20000;

    projectNameShift += sizeof(uint16_t);
}

static void recoveryProjectVersion14()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();
    strMtProjectRemote tmp;
    memcpy(&tmp, receivedProject, sizeof(tmp));

    receivedProject->values.isHeadroomEnabled = 0;
    receivedProject->values.saturationType = SaturationType::kHardClip;
    receivedProject->values.isSnapToZeroEnabled = 0;

    projectNameShift += 3 * sizeof(uint8_t);
}

static_assert(sizeof(strMtProjectRemote) ==
              2304); // if assert hits it is most likely you need to do sth here
static void recoveryProjectVersion15()
{
    strMtProjectRemote* newProject = versionRecovery.getReadedProject();
    strMtProjectRemote oldProject;
    memcpy(&oldProject, newProject, sizeof(oldProject));

    for (int trackIdx = kSampleTracks; trackIdx < kMaxAllTracksInTrackers; trackIdx++)
    {
        newProject->values.setDefaultTrackName(trackIdx);
    }

    memset(newProject->values.allPatternsBitmaskMidi,
           0,
           sizeof(newProject->values.allPatternsBitmaskMidi));
    memset(newProject->values.midiTrackMute, 0, sizeof(newProject->values.midiTrackMute));

    projectNameShift += sizeof(newProject->values.midiTrackNames);
    projectNameShift += sizeof(newProject->values.synthPart);
    projectNameShift += sizeof(newProject->values.allPatternsBitmaskMidi);
    projectNameShift += sizeof(newProject->values.midiTrackMute);

    // The name in a damaged file may have no terminator.
    strncpy(
      newProject->projectName, oldProject.projectName - projectNameShift, PROJECT_NAME_SIZE - 1);
    newProject->projectName[PROJECT_NAME_SIZE - 1] = 0;
#if FILEMANAGER_HANDLE_SYNTH == 1
    for (int part = 0; part < kSynthInstrumentsCount; part++)
    {
        TrackerImpl::getInstance()->getSynth().initSynthPartSettings(
          part, newProject->values.synthPart[part]);
    }
#endif
}

void recoveryProjectVersion16()
{
    strMtProjectRemote* receivedProject = versionRecovery.getReadedProject();
    receivedProject->values.recorderGainUsb = 0;
}

//*************************************************************************INSTRUMENT

strInstrumentFile* mtVersionRecovery::getReadedInstrument()
{
    return receivedInstrument;
}

void mtVersionRecovery::translateinstrument(uint8_t firstVersion,
                                            uint8_t lastVersion,
                                            strInstrumentFile* currentInstrument)
{
    receivedInstrument = currentInstrument;

    for (uint8_t i = firstVersion; i < lastVersion; i++)
    {
        recoveryInstrumentFunction[i - 1]();
    }
}

static void recoveryInstrumentVersion1()
{
    strInstrumentFile* receivedInstrument = versionRecovery.getReadedInstrument();

    if (receivedInstrument->instrumentDataAndHeader.instrument.playMode > 4)
    {
        receivedInstrument->instrumentDataAndHeader.instrument.playMode++;
    }
}

static void recoveryInstrumentVersion2()
{
    strInstrumentFile* receivedInstrument = versionRecovery.getReadedInstrument();

    receivedInstrument->instrumentDataAndHeader.instrument.envelope[envFinetune].delay = 0;
    receivedInstrument->instrumentDataAndHeader.instrument.envelope[envFinetune].attack = 3000;
    receivedInstrument->instrumentDataAndHeader.instrument.envelope[envFinetune].hold = 0;
    receivedInstrument->instrumentDataAndHeader.instrument.envelope[envFinetune].decay = 0;
    receivedInstrument->instrumentDataAndHeader.instrument.envelope[envFinetune].sustain = 1.0;
    receivedInstrument->instrumentDataAndHeader.instrument.envelope[envFinetune].release = 1000;
    receivedInstrument->instrumentDataAndHeader.instrument.envelope[envFinetune].amount = 1.0;
    receivedInstrument->instrumentDataAndHeader.instrument.envelope[envFinetune].loop = 0;
    receivedInstrument->instrumentDataAndHeader.instrument.envelope[envFinetune].enable = 0;
}

static void recoveryInstrumentVersion3()
{
    strInstrumentFile* receivedInstrument = versionRecovery.getReadedInstrument();

    receivedInstrument->instrumentDataAndHeader.instrument.reverbSend = 0;
}

static void recoveryInstrumentVersion4()
{
    ConvertOldFileVersionVolume volumeConverter;
    strInstrumentFile* receivedInstrument = versionRecovery.getReadedInstrument();

    receivedInstrument->instrumentDataAndHeader.instrument.volume =
      volumeConverter.convertInstrumentVolumeVersion1117to1118(
        receivedInstrument->instrumentDataAndHeader.instrument.volume);
}

static void recoveryInstrumentVersion5()
{
    strInstrumentFile* receivedInstrument = versionRecovery.getReadedInstrument();

    receivedInstrument->instrumentDataAndHeader.instrument.overdrive = 0;
}

static void recoveryInstrumentVersion6()
{
    strInstrumentFile* receivedInstrument = versionRecovery.getReadedInstrument();

    receivedInstrument->instrumentDataAndHeader.instrument.bitDepth = 16;
}

static void recoveryInstrumentVersion7()
{
    strInstrumentFile* receivedInstrument = versionRecovery.getReadedInstrument();

    receivedInstrument->instrumentDataAndHeader.instrument.tuneResample = 0;
}

static void recoveryInstrumentVersion8()
{
    ConvertOldFileVersionVolume volumeConverter;
    strInstrumentFile* receivedInstrument = versionRecovery.getReadedInstrument();

    receivedInstrument->instrumentDataAndHeader.instrument.volume =
      volumeConverter.convertInstrumentVolumeVersion1300to1302(
        receivedInstrument->instrumentDataAndHeader.instrument.volume);
}
