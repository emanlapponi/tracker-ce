#include "fileManagerDefs.h"
#include "mtStructs.h"

#include "mtSequencer.h"

#include "VoiceStreamManager.h"
#include "fileManager.h"
#include "fileTransfer.h"
#include "log.h"

#include <cstring>

/*
 * https://github.com/schismtracker/schismtracker/wiki/ITTECH.TXT
 */

uint16_t expOrdNum = 0;
uint16_t expInst = 0;
uint16_t expSmp = 0;
uint16_t expWave = 0;
uint16_t expPattern = 0;

FileSystem::FileHandle exportedFile = nullptr;

const uint8_t debugExport = 1;
const uint8_t EXPORT_MAX_PATTERN = 199;
/*
 * export wave
 */
enum enWaveWriteStat
{
    waveWrite_idle,
    waveWrite_inProgress,
    waveWrite_finished,

};

uint32_t waveTotalBytes = 0;
uint32_t waveBytesLeft = 0;
uint8_t waveWriteStatus = waveWrite_idle;

uint8_t* waveSourceDataPtr;

void buildAmpEnvelopeFromLFO(uint8_t* ptr, strInstrument* instr);
void buildPanEnvelopeFromLFO(uint8_t* ptr, strInstrument* instr);

void cFileManager::updateExportModFile()
{
    switch (currentOperationStep)
    {
        case 0: exportItFile_Init(); break;
        case 1: exportItFile_InitHeader(); break;
        case 2: exportItFile_ProcessInstruments(); break;
        case 3: exportItFile_ProcessSamples(); break;
        case 4: exportItFile_ProcessWaves(); break;
        case 5: exportItFile_ProcessPatterns(); break;
        case 6: exportItFile_Finish(); break;

        default: stopOperationWithError(fmExportModError); break;
    }
}

uint8_t cFileManager::exportItFile()
{
    stopAutosave();

    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    currentOperationStep = 0;
    status = fmExportingMod;
    currentOperation = fmExportModFile;

    return true;
}

void cFileManager::exportItFile_Init()
{
    //	Serial.println("exportItFile_Init");

    waveTotalBytes = 0;
    waveBytesLeft = 0;
    waveWriteStatus = waveWrite_idle;
    waveSourceDataPtr = (uint8_t*)externalRamSampleMemory;

    expOrdNum = 0;
    expInst = 0;
    expSmp = 0;
    expWave = 0;
    expPattern = 0;

    if (!fileSystem->exists("Export"))
        fileSystem->mkdir(0, "Export");

    char file[100];

    sprintf(file, cExportModFilePathFormat, newFileManager.getCurrentProjectName());

    if (fileSystem->exists(file))
    {
        fileSystem->remove(file);
    }

    auto [isOpen, fileHandle] =
      fileSystem->openFile(file, FileSystem::FileOpenMode::kWriteCreateNew, &exportedFile);
    if (isOpen)
    {
        moveToNextOperationStep();
    }
    else
    {
        exportItFile_Error();
    }
}

// wpisz little endian
uint8_t* writeLE(uint8_t* dest, uint32_t val, uint8_t size)
{

    switch (size)
    {
        case 1: *dest = val; break;

        case 2: *((uint16_t*)dest) = val; break;

        case 4: *((uint32_t*)dest) = val; break;

        default:
            for (int8_t a = size - 1; a >= 0; a--)
            {
                *(dest + a) = (val >> (a * 8)) & 0xff;
            }
            break;
    }

    return dest + size;
}

// wpisz little endian
uint8_t* writeLEInt(uint8_t* dest, int32_t val, uint8_t size)
{

    int8_t* destInt = (int8_t*)dest;
    switch (size)
    {
        case 1: *destInt = (int8_t)val; break;

        case 2: *((int16_t*)destInt) = (int16_t)val; break;

        case 4: *((int32_t*)destInt) = val; break;

        default:
            for (int8_t a = size - 1; a >= 0; a--)
            {
                *(destInt + a) = (val >> (a * 8)) & 0xff;
            }
            break;
    }

    return dest + size;
}

void cFileManager::exportItFile_InitHeader()
{
    //	Serial.println("exportItFile_InitHeader");

    uint8_t buff32[0x20]{ 0 };

    // 0x00
    sprintf((char*)buff32, "IMPM%.26s", newFileManager.getCurrentProjectName());

    fileSystem->writeFile(buff32, sizeof(buff32), exportedFile);

    // 0x20
    memset(buff32, 0, sizeof(buff32));

    expOrdNum = 200;
    uint16_t InsNum = kSampleInstrumentsCount;
    uint16_t SmpNum = kSampleInstrumentsCount;
    uint16_t PatNum = 199;
    uint16_t Cwt = 0x0217;
    uint16_t Cmwt = 0x200;
    uint16_t Flags = (1 << 2) | // Bit 2: On = Use instruments, Off = Use samples.
                     (1 << 3);  // Bit 3: On = Linear slides, Off = Amiga slides.

    uint16_t Special = 0b00000000;

    uint16_t GV = 128;
    uint16_t MV = 128;
    uint16_t IS = 6;
    uint16_t IT = mtProject.values.globalTempo;
    uint16_t Sep = 0;
    uint16_t PWD = 0;
    uint16_t MsgLgth = 0;
    uint16_t MsgOffset = 0;

    uint8_t* ptr = &buff32[0];
    ptr = writeLE(ptr, expOrdNum, 2);
    ptr = writeLE(ptr, InsNum, 2);
    ptr = writeLE(ptr, SmpNum, 2);
    ptr = writeLE(ptr, PatNum, 2);
    ptr = writeLE(ptr, Cwt, 2);
    ptr = writeLE(ptr, Cmwt, 2);
    ptr = writeLE(ptr, Flags, 2);
    ptr = writeLE(ptr, Special, 2);

    ptr = writeLE(ptr, GV, 1);
    ptr = writeLE(ptr, MV, 1);
    ptr = writeLE(ptr, IS, 1);
    ptr = writeLE(ptr, IT, 1);
    ptr = writeLE(ptr, Sep, 1);
    ptr = writeLE(ptr, PWD, 1);
    ptr = writeLE(ptr, MsgLgth, 2);
    ptr = writeLE(ptr, MsgOffset, 2);

    fileSystem->writeFile(buff32, sizeof(buff32), exportedFile);

    // 0x40  Chnl Pan (64 bytes)
    // 0 (absolute left) to 64 (absolute right). 32 = central pan
    uint8_t buff64[0x40]{ 0 };
    for (uint8_t a = 0; a < 64; a++)
    {
        buff64[a] = 32;
    }
    fileSystem->writeFile(buff64, sizeof(buff64), exportedFile);

    // 0x40  Chnl Vol (64 bytes)
    // 0->64
    memset(buff64, 0, sizeof(buff64));

    for (uint16_t a = 0; a < 64; a++)
    {
        buff64[a] = 64;
    }
    fileSystem->writeFile(buff64, sizeof(buff64), exportedFile);

    // 0x60  orders/song
    uint8_t buffPlaylist[expOrdNum];
    for (uint16_t a = 0; a < expOrdNum; a++)
    {
        if (mtProject.song.playlist[a] > 0)
        {
            buffPlaylist[a] = mtProject.song.playlist[a] - 1;
        }
        else
        {
            buffPlaylist[a] = 255;
        }
    }
    fileSystem->writeFile(buffPlaylist, sizeof(buffPlaylist), exportedFile);

    // instruments
    uint8_t buffInstruments[InsNum * 4];
    for (uint16_t a = 0; a < InsNum * 4; a++)
    {
        buffInstruments[a] = 0;
    }
    fileSystem->writeFile(buffInstruments, sizeof(buffInstruments), exportedFile);

    // samples
    uint8_t buffSamples[SmpNum * 4];
    for (uint16_t a = 0; a < SmpNum * 4; a++)
    {
        buffSamples[a] = 0;
    }
    fileSystem->writeFile(buffSamples, sizeof(buffSamples), exportedFile);

    // patterns
    uint8_t buffPatterns[PatNum * 4];
    for (uint16_t a = 0; a < PatNum * 4; a++)
    {
        buffPatterns[a] = 0;
    }
    fileSystem->writeFile(buffPatterns, sizeof(buffPatterns), exportedFile);

    moveToNextOperationStep();
}

void cFileManager::exportItFile_storeInstrumentOffset()
{
    uint32_t endOfFile = fileSystem->getFileSize(exportedFile);
    fileSystem->seekFile(0x00C0 + expOrdNum + expInst * 4, exportedFile);

    uint8_t buff[4];
    writeLE(buff, endOfFile, 4);

    fileSystem->writeFile(buff, 4, exportedFile);
    fileSystem->seekFile(endOfFile, exportedFile);
}

void cFileManager::exportItFile_storeSampleOffset()
{
    uint32_t endOfFile = fileSystem->getFileSize(exportedFile);
    fileSystem->seekFile(0x00C0 + expOrdNum + kSampleInstrumentsCount * 4 + expSmp * 4, exportedFile);

    uint8_t buff[4];
    writeLE(buff, endOfFile, 4);

    fileSystem->writeFile(buff, 4, exportedFile);
    fileSystem->seekFile(endOfFile, exportedFile);
}

void cFileManager::exportItFile_storePatternOffset()
{
    uint32_t endOfFile = fileSystem->getFileSize(exportedFile);
    fileSystem->seekFile(0x00C0 + expOrdNum + kSampleInstrumentsCount * 4 + kSampleInstrumentsCount * 4 +
                           expPattern * 4,
                         exportedFile);

    uint8_t buff[4];
    writeLE(buff, endOfFile, 4);

    fileSystem->writeFile(buff, 4, exportedFile);
    fileSystem->seekFile(endOfFile, exportedFile);
}

void cFileManager::exportItFile_storeWaveOffset()
{
    uint32_t endOfFile = fileSystem->getFileSize(exportedFile);
    // get sample offset

    uint32_t offSetForSampleOffset = 0x00C0 + expOrdNum + kSampleInstrumentsCount * 4 + expWave * 4;
    fileSystem->seekFile(offSetForSampleOffset, exportedFile);

    //	czytamy offset pliku sampla
    uint32_t sampleFileOffset;
    fileSystem->readFile((uint8_t*)&sampleFileOffset, 4, exportedFile);

    // wpisujemy offset wave do pliku sampla, zmienna SamplePointer z docsa
    fileSystem->seekFile(sampleFileOffset + 0x48, exportedFile);
    uint8_t buff[4];
    writeLE(buff, endOfFile, 4);
    fileSystem->writeFile(buff, sizeof(buff), exportedFile);

    // wracamy na koniec
    fileSystem->seekFile(endOfFile, exportedFile);
}

const float tSRAmp[24] = {
    24,       16,    12,       8,      6,        4,        3,        2,
    1.5,      1,     0.75,     0.5,    0.375,    0.333333, 0.25,     0.1875,
    0.166667, 0.125, 0.083333, 0.0625, 0.041667, 0.03125,  0.020833, 0.015625
};

void buildAmpEnvelopeFromLFO(uint8_t* ptr, strInstrument* instr)
{

    LOGF("shape: %d, amount: %.1f, step factor : %f\n",
         instr->lfo[0].shape,
         instr->lfo[0].amount,
         tSRAmp[instr->lfo[0].speed]);

    uint16_t periodTicks = tSRAmp[instr->lfo[0].speed] * 6;
    periodTicks = clamp<int>(periodTicks, 2, 9999);

    LOGF("period ticks: %d\n", periodTicks);

    uint8_t minVal = 0;

    if (instr->lfo[0].shape == lfoShapeSquare)
    {

        ptr = writeLE(ptr,
                      ((instr->envelope[0].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 4, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 3, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[0].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks / 2 <= tempTickPos ? tempTickPos + 1 : periodTicks / 2;
        // node 1
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks / 2 <= tempTickPos ? tempTickPos + 1 : periodTicks / 2;
        // node 2
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;
        // node 3
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[0].shape == lfoShapeSaw)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[0].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 3, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 2, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[0].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 1
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 2
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[0].shape == lfoShapeReverseSaw)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[0].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 3, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 2, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[0].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos++;
        //				periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 1
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 2
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[0].shape == lfoShapeTriangle)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[0].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 3, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 2, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[0].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks / 2 <= tempTickPos ? tempTickPos + 1 : periodTicks / 2;
        // node 1
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;
        // node 2
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[0].shape == lfoShapeRandom)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[0].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 6, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 5, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[0].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 2 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 2 / 6;
        // node 1
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 3 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 3 / 6;
        // node 2
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 4 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 4 / 6;

        // node 3
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 5 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 5 / 6;

        // node 4
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;
        // node 5ó
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
}

void buildFilterEnvelopeFromLFO(uint8_t* ptr, strInstrument* instr)
{

    LOGF("shape: %d, amount: %.1f, step factor : %f\n",
         instr->lfo[2].shape,
         instr->lfo[2].amount,
         tSRAmp[instr->lfo[2].speed]);

    uint16_t periodTicks = tSRAmp[instr->lfo[2].speed] * 6;
    periodTicks = clamp<int>(periodTicks, 2, 9999);

    LOGF("period ticks: %d\n", periodTicks);

    uint8_t minVal = 0;

    if (instr->lfo[2].shape == lfoShapeSquare)
    {

        ptr = writeLE(ptr,
                      //						((instr->envelope[2].enable > 0) << 0) | // on/off
                      ((1) << 0) | // on/off (z cutoffem jest problem, lepiej jak jest wlaczony)
                        (0 << 1) | // loop
                        (1 << 2) | // sus loop
                        (1 << 7),  // Bit 7: Use pitch envelope as filter envelope instead.
                      1);          // Flg
        ptr = writeLE(ptr, 4, 1);  // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);  // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);  // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);  // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 3, 1);  // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[2].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks / 2 <= tempTickPos ? tempTickPos + 1 : periodTicks / 2;
        // node 1
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks / 2 <= tempTickPos ? tempTickPos + 1 : periodTicks / 2;
        // node 2
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;
        // node 3
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[2].shape == lfoShapeSaw)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[2].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 3, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 2, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[2].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 1
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 2
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[2].shape == lfoShapeReverseSaw)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[2].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 3, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 2, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[2].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos++;
        //				periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 1
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 2
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[2].shape == lfoShapeTriangle)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[2].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 3, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 2, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[2].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks / 2 <= tempTickPos ? tempTickPos + 1 : periodTicks / 2;
        // node 1
        ptr = writeLE(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;
        // node 2
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[2].shape == lfoShapeRandom)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[2].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 6, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 5, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[2].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLE(
          ptr, minVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 2 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 2 / 6;
        // node 1
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 3 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 3 / 6;
        // node 2
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 4 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 4 / 6;

        // node 3
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 5 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 5 / 6;

        // node 4
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;
        // node 5ó
        ptr = writeLE(ptr,
                      utilMathRandom::random(0, maxValue),
                      1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
}

void buildPanEnvelopeFromLFO(uint8_t* ptr, strInstrument* instr)
{

    LOGF("shape: %d, amount: %.1f, step factor : %f\n",
         instr->lfo[1].shape,
         instr->lfo[1].amount,
         tSRAmp[instr->lfo[1].speed]);

    uint16_t periodTicks = tSRAmp[instr->lfo[1].speed] * 6;
    periodTicks = clamp<int>(periodTicks, 2, 9999);

    LOGF("period ticks: %d\n", periodTicks);

    int8_t minValue = clamp<int>(instr->lfo[1].amount * -32, -32, 0);

    if (instr->lfo[1].shape == lfoShapeSquare)
    {

        ptr = writeLE(ptr,
                      ((instr->envelope[1].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 4, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 3, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[1].amount * 32, 0, 32);
        // square ___---___---
        // node 0
        ptr = writeLEInt(
          ptr, minValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks / 2 <= tempTickPos ? tempTickPos + 1 : periodTicks / 2;
        // node 1
        ptr = writeLEInt(
          ptr, minValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks / 2 <= tempTickPos ? tempTickPos + 1 : periodTicks / 2;
        // node 2
        ptr = writeLEInt(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;
        // node 3
        ptr = writeLEInt(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[1].shape == lfoShapeSaw)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[1].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 3, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 2, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[1].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLEInt(
          ptr, minValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 1
        ptr = writeLEInt(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 2
        ptr = writeLEInt(
          ptr, minValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[1].shape == lfoShapeReverseSaw)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[1].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 3, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 2, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[1].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLEInt(
          ptr, minValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos++;
        //				periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 1
        ptr = writeLEInt(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;

        // node 2
        ptr = writeLEInt(
          ptr, minValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[1].shape == lfoShapeTriangle)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[1].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 3, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 2, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[1].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLEInt(
          ptr, minValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks / 2 <= tempTickPos ? tempTickPos + 1 : periodTicks / 2;
        // node 1
        ptr = writeLEInt(
          ptr, maxValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;
        // node 2
        ptr = writeLEInt(
          ptr, minValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
    else if (instr->lfo[1].shape == lfoShapeRandom)
    {
        ptr = writeLE(ptr,
                      ((instr->envelope[1].enable > 0) << 0) | // on/off
                        (0 << 1) |                             // loop
                        (1 << 2),                              // sus loop
                      1);                                      // Flg
        ptr = writeLE(ptr, 6, 1);                              // Num = Number of node points
        ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
        ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
        ptr = writeLE(ptr, 0, 1);                              // SLB = Sustain loop beginning
        ptr = writeLE(ptr, 5, 1);                              // SLE = Sustain loop end

        uint16_t tempTickPos = 0;
        uint8_t maxValue = clamp<int>(instr->lfo[1].amount * 64, 0, 64);
        // square ___---___---
        // node 0
        ptr = writeLEInt(
          ptr, minValue, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 2 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 2 / 6;
        // node 1
        ptr = writeLEInt(ptr,
                         utilMathRandom::random(minValue, maxValue),
                         1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 3 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 3 / 6;
        // node 2
        ptr = writeLEInt(ptr,
                         utilMathRandom::random(minValue, maxValue),
                         1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 4 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 4 / 6;

        // node 3
        ptr = writeLEInt(ptr,
                         utilMathRandom::random(minValue, maxValue),
                         1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks * 5 / 6 <= tempTickPos ? tempTickPos + 1 : periodTicks * 5 / 6;

        // node 4
        ptr = writeLEInt(ptr,
                         utilMathRandom::random(minValue, maxValue),
                         1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)

        tempTickPos = periodTicks <= tempTickPos ? tempTickPos + 1 : periodTicks;
        // node 5ó
        ptr = writeLEInt(ptr,
                         utilMathRandom::random(minValue, maxValue),
                         1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
        ptr = writeLE(ptr, tempTickPos, 2); // 1 word (2 bytes) for tick number (0->9999)
    }
}

void cFileManager::exportItFile_ProcessInstruments()
{

    strInstrument* instr = &mtProject.instrument[expInst];
    exportItFile_storeInstrumentOffset();

    uint8_t buff0x20[0x20]{ 0 };

    // 0x00
    sprintf((char*)buff0x20, "IMPIInstr%.3d.mti", expInst + 1);

    uint8_t* ptr = &buff0x20[0x10];
    ptr = writeLE(ptr, 0, 1);   // 00h
    ptr = writeLE(ptr, 2, 1);   // NNA
    ptr = writeLE(ptr, 0, 1);   // DCT
    ptr = writeLE(ptr, 0, 1);   // DCA
    ptr = writeLE(ptr, 250, 2); // FadeOut
    ptr = writeLE(ptr, 0, 1);   // PPS
    ptr = writeLE(
      ptr, 60, 1); //        PPC: Pitch-Pan center: C-0 to B-9 represented as 0->119 inclusive
    ptr = writeLE(ptr, 128, 1); //     GbV: Global Volume, 0->128
    ptr = writeLE(ptr, 32, 1);  //        DfP: Default Pan, 0->64, &128 => Don't use
    ptr = writeLE(ptr, 26, 1);  //        RV: Random volume variation (percentage)
    ptr = writeLE(
      ptr, 21, 1); //        RP: Random panning variation (panning change - not implemented yet)
    ptr = writeLE(ptr, 529, 2); // TrkVers
    ptr = writeLE(ptr, 1, 1);   // NoS
    ptr = writeLE(ptr, 0, 1);   // x

    fileSystem->writeFile(buff0x20, sizeof(buff0x20), exportedFile);

    // 0020: ? Instrument Name, max 26 bytes, includes NUL...................?
    memset(buff0x20, 0, sizeof(buff0x20));

    sprintf((char*)buff0x20, "%.25s", instr->sample.file_name);

    ptr = &buff0x20[0x1a];
    ptr = writeLE(ptr, 0, 1); // IFC = Initial Filter cutoff
    ptr = writeLE(ptr, 0, 1); // IFR = Initial Filter resonance
    ptr = writeLE(ptr, 0, 1); // MCh = MIDI Channel
    ptr = writeLE(ptr, 0, 1); // MPr = MIDI Program (Instrument)
    ptr = writeLE(ptr, 0, 2); // MIDIBnk ???

    fileSystem->writeFile(buff0x20, sizeof(buff0x20), exportedFile);

    // Note-Sample/Keyboard Table, Length = 240 bytes
    uint8_t buffNoteSample[240];
    for (uint16_t a = 0, buffPos = 0; a < 120; a++)
    {
        buffNoteSample[buffPos++] = a;
        buffNoteSample[buffPos++] = expInst + 1;
    }
    fileSystem->writeFile(buffNoteSample, sizeof(buffNoteSample), exportedFile);

    // pseudoenvelope

    /*
     * volume envelope
     */

    uint8_t buff0x52[0x52]{ 0 };
    {
        uint16_t attTicks = 0;
        uint16_t decTicks = 0;
        uint16_t relTicks = 0;
        uint16_t sustainVal = 0;

        if (instr->envelope[0].enable && !instr->envelope[0].loop)
        {
            attTicks = (float)instr->envelope[0].attack /
                       (float)(10000.0f / (mtProject.values.globalTempo * 4));
            if (attTicks < 1)
                attTicks = 1;

            decTicks = (float)instr->envelope[0].decay /
                       (float)(10000.0f / (mtProject.values.globalTempo * 4));
            if (decTicks < 1)
                decTicks = 1;

            relTicks = (float)instr->envelope[0].release /
                       (float)(10000.0f / (mtProject.values.globalTempo * 4));
            if (relTicks < 1)
                relTicks = 1;

            sustainVal = mapClamped<float>(instr->envelope[0].sustain, 0.0f, 1.0f, 0, 64);
            if (sustainVal > 64)
                sustainVal = 64;

            ptr = buff0x52;
            ptr = writeLE(ptr,
                          ((instr->envelope[0].enable > 0) << 0) | // on/off
                            (0 << 1) |                             // loop
                            (1 << 2),                              // sus loop
                          1);                                      // Flg
            ptr = writeLE(ptr, 5, 1);                              // Num = Number of node points
            ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
            ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
            ptr = writeLE(ptr, 2, 1);                              // SLB = Sustain loop beginning
            ptr = writeLE(ptr, 3, 1);                              // SLE = Sustain loop end

            // node 0
            ptr = writeLE(
              ptr, 0, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
            ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

            // node 1
            ptr = writeLE(ptr, 64, 1);               // att max
            ptr = writeLE(ptr, attTicks /*att*/, 2); // att time

            // node 2
            ptr = writeLE(ptr, sustainVal, 1);                  // sus val
            ptr = writeLE(ptr, attTicks + decTicks /*dec*/, 2); // decay time

            // node 3
            ptr = writeLE(ptr, sustainVal /*sus*/, 1);       // sus val
            ptr = writeLE(ptr, attTicks + decTicks + 10, 2); // time nie gra roli bo loop

            // node 4
            ptr = writeLE(ptr, 0, 1);                                           // release val 0
            ptr = writeLE(ptr, attTicks + decTicks + 10 + relTicks /*rel*/, 2); // time = release
        }
        else if (instr->envelope[0].enable && instr->envelope[0].loop)
        {
            ptr = buff0x52;
            buildAmpEnvelopeFromLFO(ptr, instr);
        }

        fileSystem->writeFile(buff0x52, sizeof(buff0x52), exportedFile);
    }

    /*
     * panning envelope
     */
    {

        memset(buff0x52, 0, sizeof(buff0x52));

        uint16_t attTicks = 1;
        uint16_t decTicks = 1;
        uint16_t relTicks = 1;
        uint16_t sustainVal = 64;

        if (instr->envelope[1].enable && !instr->envelope[1].loop)
        {
            attTicks = (float)instr->envelope[1].attack /
                       (float)(10000.0f / (mtProject.values.globalTempo * 4));
            decTicks = (float)instr->envelope[1].decay /
                       (float)(10000.0f / (mtProject.values.globalTempo * 4));
            relTicks = (float)instr->envelope[1].release /
                       (float)(10000.0f / (mtProject.values.globalTempo * 4));
            sustainVal = mapClamped<float>(instr->envelope[1].sustain, 0.0f, 1.0f, 0, 32);
            if (sustainVal > 32)
                sustainVal = 32;

            ptr = buff0x52;
            ptr = writeLE(ptr,
                          ((instr->envelope[1].enable > 0) << 0) | // on/off
                            (0 << 1) |                             // loop
                            (1 << 2),                              // sus loop
                          1);                                      // Flg
            ptr = writeLE(ptr, 5, 1);                              // Num = Number of node points
            ptr = writeLE(ptr, 0, 1);                              // LpB = Loop beginning
            ptr = writeLE(ptr, 0, 1);                              // LpE = Loop end
            ptr = writeLE(ptr, 2, 1);                              // SLB = Sustain loop beginning
            ptr = writeLE(ptr, 3, 1);                              // SLE = Sustain loop end

            // node 0
            ptr = writeLE(
              ptr, 0, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
            ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

            // node 1
            ptr = writeLE(ptr, 32, 1);               // att max
            ptr = writeLE(ptr, attTicks /*att*/, 2); // att time

            // node 2
            ptr = writeLE(ptr, sustainVal, 1);                  // sus val
            ptr = writeLE(ptr, attTicks + decTicks /*dec*/, 2); // decay time

            // node 3
            ptr = writeLE(ptr, sustainVal /*sus*/, 1);       // sus val
            ptr = writeLE(ptr, attTicks + decTicks + 10, 2); // time nie gra roli bo loop

            // node 4
            ptr = writeLE(ptr, 0, 1);                                           // release val 0
            ptr = writeLE(ptr, attTicks + decTicks + 10 + relTicks /*rel*/, 2); // time = release
        }
        else if (instr->envelope[1].enable && instr->envelope[1].loop)
        {
            ptr = buff0x52;
            buildPanEnvelopeFromLFO(ptr, instr);
        }

        fileSystem->writeFile(buff0x52, sizeof(buff0x52), exportedFile);
    }

    /*
     * pitch/cutoff envelope
     */
    {
        memset(buff0x52, 0, sizeof(buff0x52));
        uint16_t attTicks = 1;
        uint16_t attVal = 64;
        uint16_t decTicks = 1;
        uint16_t relTicks = 1;
        uint16_t relVal = 64;
        uint16_t sustainVal = 64;

        if (instr->envelope[2].enable && !instr->envelope[0].loop)
        {
            attTicks = (float)instr->envelope[2].attack /
                       (float)(10000.0f / (mtProject.values.globalTempo * 4));
            attVal = 0;
            decTicks = (float)instr->envelope[2].decay /
                       (float)(10000.0f / (mtProject.values.globalTempo * 4));
            relTicks = (float)instr->envelope[2].release /
                       (float)(10000.0f / (mtProject.values.globalTempo * 4));
            relVal = 0;
            sustainVal = mapClamped<float>(instr->envelope[2].sustain, 0.0f, 1.0f, 0, 64);
            if (sustainVal > 64)
                sustainVal = 64;

            ptr = buff0x52;
            ptr =
              writeLE(ptr,
                      //						((instr->envelope[2].enable > 0) << 0) | // on/off
                      ((1) << 0) |    // on/off (z cutoffem jest problem, lepiej jak jest wlaczony)
                        (0 << 1) |    // loop
                        (1 << 2) |    // sus loop
                        (1 << 7),     // Bit 7: Use pitch envelope as filter envelope instead.
                      1);             // Flg
            ptr = writeLE(ptr, 5, 1); // Num = Number of node points
            ptr = writeLE(ptr, 0, 1); // LpB = Loop beginning
            ptr = writeLE(ptr, 0, 1); // LpE = Loop end
            ptr = writeLE(ptr, 2, 1); // SLB = Sustain loop beginning
            ptr = writeLE(ptr, 3, 1); // SLE = Sustain loop end

            // node 0
            ptr = writeLE(
              ptr, attVal, 1); // 1 byte for y-value//(0->64 for vol, -32->+32 for panning or pitch)
            ptr = writeLE(ptr, 0, 2); // 1 word (2 bytes) for tick number (0->9999)

            // node 1
            ptr = writeLE(ptr, 32, 1);               // att max
            ptr = writeLE(ptr, attTicks /*att*/, 2); // att time

            // node 2
            ptr = writeLE(ptr, sustainVal, 1);                  // sus val
            ptr = writeLE(ptr, attTicks + decTicks /*dec*/, 2); // decay time

            // node 3
            ptr = writeLE(ptr, sustainVal /*sus*/, 1);       // sus val
            ptr = writeLE(ptr, attTicks + decTicks + 10, 2); // time nie gra roli bo loop

            // node 4
            ptr = writeLE(ptr, relVal, 1);                                      // release val 0
            ptr = writeLE(ptr, attTicks + decTicks + 10 + relTicks /*rel*/, 2); // time = release
        }
        else if (instr->envelope[2].enable && instr->envelope[0].loop)
        {
            ptr = buff0x52;
            buildFilterEnvelopeFromLFO(ptr, instr);
        }

        fileSystem->writeFile(buff0x52, sizeof(buff0x52), exportedFile);
    }

    expInst++;
    if (expInst >= kSampleInstrumentsCount)
    {
        moveToNextOperationStep();
    }
}

void cFileManager::exportItFile_ProcessSamples()
{
    strInstrument* instr = &mtProject.instrument[expSmp];
    exportItFile_storeSampleOffset();

    uint8_t buff0x50[0x50]{ 0 };

    // 0x00
    sprintf((char*)buff0x50, "IMPSInstr%.3d.mts", expSmp + 1);

    bool isLoop = instr->playMode == playModePingpong || instr->playMode == playModeLoopForward ||
                  instr->playMode == playModeLoopBackward;
    bool isLoopPingPong = instr->playMode == playModePingpong;

    uint8_t instrVolume = mapClamped<int>(instr->volume, 0, 100, 0, 64);

    uint8_t Flg =
      (1 << 0) |              // Bit 0. On = sample associated with header.
      (1 << 1) |              // Bit 1. On = 16 bit, Off = 8 bit.
      (0 << 2) |              // Bit 2. On = stereo, Off = mono. Stereo samples not supported yet
      (0 << 3) |              // Bit 3. On = compressed samples.
      (isLoop << 4) |         // Bit 4. On = Use loop
      (isLoop << 5) |         // Bit 5. On = Use sustain loop
      (isLoopPingPong << 6) | // Bit 6. On = Ping Pong loop, Off = Forwards loop
      (isLoopPingPong << 7);  // Bit 7. On = Ping Pong Sustain loop, Off = Forwards Sustain loop

    uint8_t* ptr = &buff0x50[0x10];
    ptr = writeLE(ptr, 0, 1);           // 00h
    ptr = writeLE(ptr, instrVolume, 1); // GvL:      Global volume for instrument, ranges from 0->64
    ptr = writeLE(ptr, Flg, 1);         // Flg
    ptr = writeLE(ptr, instrVolume, 1); // Vol

    sprintf((char*)ptr, "%.25s", instr->sample.file_name);

    ptr = &buff0x50[0x2e];
    ptr = writeLE(ptr, 0b00000001, 1); // cvt

    ptr = &buff0x50[0x30];
    ptr = writeLE(ptr, instr->sample.length, 4); //	Length
    ptr =
      writeLE(ptr,
              mapClamped<float>((float)instr->loopPoint1, 0, MAX_16BIT, 0, instr->sample.length),
              4); //	Loop Begin
    ptr =
      writeLE(ptr,
              mapClamped<float>((float)instr->loopPoint2, 0, MAX_16BIT, 0, instr->sample.length),
              4);                 //	Loop End
    ptr = writeLE(ptr, 44100, 4); //	C5Speed

    ptr =
      writeLE(ptr,
              mapClamped<float>((float)instr->loopPoint1, 0, MAX_16BIT, 0, instr->sample.length),
              4); //	SusLoop Begin
    ptr =
      writeLE(ptr,
              mapClamped<float>((float)instr->loopPoint2, 0, MAX_16BIT, 0, instr->sample.length),
              4); //	SusLoop End

    ptr = writeLE(ptr, 0, 4); //	SamplePointer - todo tymczasowe zero
    ptr = writeLE(ptr, 0, 1); //	ViS:      Vibrato Speed, ranges from 0->64
    ptr = writeLE(ptr, 0, 1); //	ViD:      Vibrato Depth, ranges from 0->64
    ptr = writeLE(ptr, 0, 1); //	ViR:      Vibrato Rate, rate at which vibrato is applied (0->64)
    ptr = writeLE(ptr, 0, 1); //	ViT:      Vibrato waveform type.

    fileSystem->writeFile(buff0x50, sizeof(buff0x50), exportedFile);

    expSmp++;
    if (expSmp >= kSampleInstrumentsCount)
    {
        moveToNextOperationStep();
    }
}

void cFileManager::exportItFile_ProcessWaves()
{
    strInstrument* instr = &mtProject.instrument[expWave];

    if (waveWriteStatus == waveWrite_idle)
    {
        exportItFile_storeWaveOffset();

        if (instr->isActive && instr->sample.address != nullptr)
        {
            waveSourceDataPtr = (uint8_t*)instr->sample.address;
        }

        waveTotalBytes = instr->sample.length * 2;
        waveBytesLeft = waveTotalBytes;
        waveWriteStatus = waveWrite_inProgress;
    }

    if (waveWriteStatus == waveWrite_inProgress)
    {
        uint32_t bytesToWrite = waveBytesLeft;
        if (bytesToWrite > 512)
            bytesToWrite = 512;

        if (mtProject.sampleMemory[expWave].isStreamed && bytesToWrite > 0)
        {
            int16_t tempWaveBuf[256];
            uint32_t frameOffset = (waveTotalBytes - waveBytesLeft) / 2;
            uint32_t frameCount = bytesToWrite / 2;
            voiceStreamManager.readInstrumentFramesBlocking(
              expWave, 0, frameOffset, frameCount, tempWaveBuf);
            fileSystem->writeFile(tempWaveBuf, bytesToWrite, exportedFile);
        }
        else
        {
            fileSystem->writeFile(waveSourceDataPtr, bytesToWrite, exportedFile);
        }

        waveSourceDataPtr += bytesToWrite; // przesuwam wskaznik
        waveBytesLeft -= bytesToWrite;     // zmniejszam ikosc danych do kopiowania

        if (waveBytesLeft <= 0)
        {
            waveWriteStatus = waveWrite_finished;
        }
    }
    if (waveWriteStatus == waveWrite_finished)
    {
        waveWriteStatus = waveWrite_idle;

        expWave++;
        if (expWave >= kSampleInstrumentsCount)
        {
            moveToNextOperationStep();
        }
    }
}
/*
 *  if(maskvariable & 1), then read note. (byte value)
 // Note ranges from 0->119 (C-0 -> B-9)
 // 255 = note off, 254 = notecut
 // Others = note fade (already programmed into IT's player
 //                     but not available in the editor)
 */
void cFileManager::exportItFile_ProcessPatterns()
{
    // expPattern= 0;

    if (newFileManager.loadWorkspacePatternNow(expPattern + 1))
    {
        exportItFile_storePatternOffset();
        sequencer.switchRamPatternsNow();
    }
    else
    {
        expPattern++;
        if (expPattern >= EXPORT_MAX_PATTERN)
        {
            moveToNextOperationStep();
        }
        return;
    }

    Sequencer::strPattern* patt = sequencer.getActualPattern();
    uint32_t seekForLength = fileSystem->getFileSize(exportedFile); // pozycja gdzie wpisać length

    uint8_t buff0x08[0x08]{ 0 };

    uint8_t* ptr = &buff0x08[0x00];
    ptr = writeLE(ptr, 0, 2);                         // length todo: wpisać pozniej
    ptr = writeLE(ptr, patt->track[0].length + 1, 2); // rows

    fileSystem->writeFile(buff0x08, sizeof(buff0x08), exportedFile);

    uint16_t length = 0;

    for (uint8_t stepIdx = 0; stepIdx <= patt->track[0].length; stepIdx++)
    {
        for (uint8_t tr = 0; tr < kAllTracks; tr++)
        {
            Sequencer::strPattern::strTrack::strStep* step = &patt->track[tr].step[stepIdx];

            if (step->note != Sequencer::STEP_NOTE_EMPTY)
            {
                uint8_t channelvariable = 0;
                uint8_t noteToWrite = 0;
                uint8_t instrumentToWrite = 0;
                uint8_t volPanToWrite = 0;

                // ustawiam channel
                channelvariable = (tr + 1) & 0b00111111;

                // ustawiamy flage ze kolejny bajt to maskvariable
                channelvariable |= 0b10000000;

                fileSystem->writeFile(&channelvariable, sizeof(channelvariable), exportedFile);

                length++;

                /*
                 * MASKA
                 */
                // maska mowi co maja kolejne bajty
                uint8_t maskvariable = 0;
                maskvariable |= 1; // note w kolejnym bajcie
                maskvariable |= 2; // instr w kolejnym bajcie
                /*
                 *
                 */
                // velocity
                if (step->fx[0].type == sequencer.fx.FX_TYPE_VOLUME)
                {
                    maskvariable |= 4; // vol/pan w kolejnym bajcie
                    volPanToWrite = mapClamped<int>(step->fx[0].value,
                                                    sequencer.getFxMin(sequencer.fx.FX_TYPE_VOLUME),
                                                    sequencer.getFxMax(sequencer.fx.FX_TYPE_VOLUME),
                                                    0,
                                                    64);

                    volPanToWrite = clamp<int>(volPanToWrite, 0, 64);
                }
                else if (step->fx[1].type == sequencer.fx.FX_TYPE_VOLUME)
                {
                    maskvariable |= 4; // vol/pan w kolejnym bajcie
                    volPanToWrite = mapClamped<int>(step->fx[1].value,
                                                    sequencer.getFxMin(sequencer.fx.FX_TYPE_VOLUME),
                                                    sequencer.getFxMax(sequencer.fx.FX_TYPE_VOLUME),
                                                    0,
                                                    64);
                    volPanToWrite = clamp<int>(volPanToWrite, 0, 64);
                }
                /*
                 *
                 */
                // panning
                if (step->fx[0].type == sequencer.fx.FX_TYPE_PANNING)
                {
                    maskvariable |= 4; // vol/pan w kolejnym bajcie
                    volPanToWrite = mapClamped<int>(step->fx[0].value,
                                                    sequencer.getFxMin(sequencer.fx.FX_TYPE_VOLUME),
                                                    sequencer.getFxMax(sequencer.fx.FX_TYPE_VOLUME),
                                                    128,
                                                    192);
                    volPanToWrite = clamp<int>(volPanToWrite, 128, 192);
                }
                else if (step->fx[1].type == sequencer.fx.FX_TYPE_PANNING)
                {
                    maskvariable |= 4; // vol/pan w kolejnym bajcie
                    volPanToWrite = mapClamped<int>(step->fx[1].value,
                                                    sequencer.getFxMin(sequencer.fx.FX_TYPE_VOLUME),
                                                    sequencer.getFxMax(sequencer.fx.FX_TYPE_VOLUME),
                                                    128,
                                                    192);
                    volPanToWrite = clamp<int>(volPanToWrite, 128, 192);
                }

                fileSystem->writeFile(&maskvariable, sizeof(maskvariable), exportedFile);
                length++;

                /*
                 * pozostale po masce
                 */

                // nuta
                if (step->note >= 0)
                    noteToWrite = step->note;
                else if (step->note == Sequencer::STEP_NOTE_OFF_DEFAULT)
                {
                    noteToWrite = 255;
                }
                else if (step->note == Sequencer::STEP_NOTE_OFF_CUT ||
                         step->note == Sequencer::STEP_NOTE_OFF_FADE)
                {
                    noteToWrite = 254;
                }
                fileSystem->writeFile(&noteToWrite, sizeof(noteToWrite), exportedFile);
                length++;

                instrumentToWrite = step->instrument + 1;
                fileSystem->writeFile(&instrumentToWrite, sizeof(instrumentToWrite), exportedFile);
                length++;

                if (maskvariable & 4)
                {
                    fileSystem->writeFile(&volPanToWrite, sizeof(volPanToWrite), exportedFile);
                    length++;
                }
            }

            if (tr == kAllTracks - 1)
            {
                uint8_t tempZero = 0;
                fileSystem->writeFile(&tempZero, sizeof(tempZero), exportedFile); // END OF ROW

                length++;
            }
        }
    }

    // wpisuje length do struktury patternu

    uint32_t seekBuffer = fileSystem->getFileSize(exportedFile);

    fileSystem->seekFile(seekForLength, exportedFile);

    // zapisuje length
    uint8_t buff[2];
    writeLE(buff, length, 2);
    fileSystem->writeFile(buff, sizeof(buff), exportedFile);
    // wracam na koniec
    fileSystem->seekFile(seekBuffer, exportedFile);
    expPattern++;
    if (expPattern >= EXPORT_MAX_PATTERN)
    {
        moveToNextOperationStep();
    }
}

void cFileManager::exportItFile_Finish()
{
    if (fileSystem->isFileOpen(exportedFile))
    {
        fileSystem->closeFile(exportedFile, false);
    }

    status = fmExportModEnd;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}

void cFileManager::exportItFile_Error()
{
    if (fileSystem->isFileOpen(exportedFile))
    {
        fileSystem->closeFile(exportedFile, false);
    }
    stopOperationWithError(fmExportModError);
}

uint8_t cFileManager::exportItFile_getProgress()
{
    float retVal = expInst + expSmp + expWave + expPattern;
    retVal = 100 * retVal / (kSampleInstrumentsCount * 3 + EXPORT_MAX_PATTERN);
    retVal = clamp(retVal, 0.0f, 100.0f);
    return (uint8_t)retVal;
}
