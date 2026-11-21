#include "mtAudioExport.h"
#include "VoiceStreamManager.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"

#include "core/songTimer.h"
#include "patternEditor/patternEditor.h"

#include "log.h"
#include <algorithm>
#include <cr_section_macros.h>

#define EXPORT_DEBUG 0
/*
 * block to 128próbek * 2kanaly stereo* 2bajty
 * packet to zestaw blokow jeden po drugim
 * Multipacket to zestaw packetsow wszystkich stemow jeden po drugim
 */

static constexpr uint16_t sizeofAudioBlock =
  AUDIO_BLOCK_SAMPLES * 2 * sizeof(int16_t); // rozmiar bloku w bajtach

static constexpr uint32_t sizeofPacket =
  sizeofAudioBlock * numOfAudioBlocksInPacket; // rozmiar pakietu jednego stemu
static constexpr uint32_t sizeofMultipacket =
  sizeofPacket * mtAudioExport::stem_totalCount; // rozmiar pakietow wszystkich stemow

static constexpr uint16_t numOfBlocksInMultiblock =
  numOfAudioBlocksInPacket * mtAudioExport::stem_totalCount;

extern uint8_t externalRamScratchBuff[];
static auto& renderBuffer = *reinterpret_cast<
  int16_t(*)[mtAudioExport::stem_totalCount][numOfAudioBlocksInPacket][2 * AUDIO_BLOCK_SAMPLES]>(
  externalRamScratchBuff);
static_assert(sizeof(renderBuffer) <= SHARED_SCRATCH_MEMORY_SIZE,
              "renderBuffer exceeds shared scratch size");

extern void artificial_isr();
extern void updateAudioEngine();

extern void muteAudioOutByKey();
extern void unmuteAudioOutByKey();

__NOINIT(EXTERNAL_RAM) static char stemsPaths[mtAudioExport::stem_totalCount][PATCH_SIZE];
__NOINIT(EXTERNAL_RAM) static char stemsRawPath[PATCH_SIZE];

mtAudioExport exporter;

FileSystem::FileHandle rawStemsFile;
FileSystem::FileHandle wavStemsFile;

void cbkSetCurrentExporterOnLastStep()
{
    exporter.setOnLastStep();
}
void emptyVoid()
{
}

void mtAudioExport::init(FileSystem* fileSystem)
{
    this->fileSystem = fileSystem;
}

void mtAudioExport::setOnLastStep()
{
    sequencer.stop();
    lastSequenceStep = true;
}

void mtAudioExport::setExternalSettings()
{
    sequencer.detachFromTimer();

    AudioStream::renderMode = 1;
    AudioPlayMemory::initNotesLookup(false);

    elapsedMillisAudio::enterVirtualTimeMode();
    elapsedMicrosAudio::enterVirtualTimeMode();

    engine.getSdPlayer()->stop();
    engine.muteHeadphones();
    muteAudioOutByKey();

    engine.disableLineInImpact();
    engine.clearDelay();
    engine.clearReverb();

    switch (selectedMode)
    {
            // @formatter:off
        case exportSongStems:
        case exportSong: sequencer.setOnSongEnd(cbkSetCurrentExporterOnLastStep); break;
        case exportPatternStems:
        case exportPattern:
            sequencer.setOnPatternOrSelectionEnd(cbkSetCurrentExporterOnLastStep);
            break;
        default: sequencer.setOnPatternOrSelectionEnd(cbkSetCurrentExporterOnLastStep); break;
    }
}

void mtAudioExport::callBackExternalSettings()
{
    for (uint8_t tr = 0; tr < stem_totalCount; tr++)
    {
        engine.getExportQueues()
          ->getQueue(ExportQueues::ExportChannels::kLeft,
                     (mtAudioExport::enStem)(mtAudioExport::enStem::stem_tracks_start + tr))
          ->end();
        engine.getExportQueues()
          ->getQueue(ExportQueues::ExportChannels::kRight,
                     (mtAudioExport::enStem)(mtAudioExport::enStem::stem_tracks_start + tr))
          ->end();
    }

    AudioStream::renderMode = 0;
    AudioPlayMemory::initNotesLookup(true);

    elapsedMillisAudio::exitVirtualTimeMode();
    elapsedMicrosAudio::exitVirtualTimeMode();

    engine.unmuteHeadphones();
    unmuteAudioOutByKey();

    engine.enableLineInImpact();

    sequencer.setOnPatternOrSelectionEnd(emptyVoid);
    sequencer.setOnSongEnd(emptyVoid);
    sequencer.attachToTimer();
}
void mtAudioExport::finishRender()
{

    sequencer.stop();

    if (exportOnlyMaster()) // master tu sie konczy
    {
        saveMasterBufferToSD(1);
        status = statusIdle;
        callBackExternalSettings();
    }
    else // stemy teraz raw do wav
    {
        saveRawStemsBufferToSD(1);
        status = statusRawToWavs_init;
    }

    [[maybe_unused]] uint32_t totalSongTime_ms = songTimeCounter;
    [[maybe_unused]] uint32_t totalExportTime_ms = totalExportTimer;

    if (EXPORT_DEBUG)
    {
        LOGF("render %ds track took %ds\n", totalSongTime_ms / 1000, totalExportTime_ms / 1000);
    }

    renderFinished = true;
}

void mtAudioExport::start(const char* path, uint8_t mode)
{
    totalExportTimer = 0;

    lastSequenceStep = false;
    selectedMode = (enExportType)mode;
    maxBlocksForStem = numOfAudioBlocksInPacket;
    songLength = 1;
    seqTimerRemainder = 0.0;
    consecutiveEmptyBuffers = 0;
    buffersAfterStop = 0;

    renderFinished = false;

    setExternalSettings();

    for (uint8_t stemIdx = 0; stemIdx < stem_totalCount; stemIdx++)
    {
        if (!isStemActive(stemIdx))
            continue;

        maxStemInCurrentExport = stemIdx;

        bufferedBlocks[stemIdx] = 0;

        switch (stemIdx)
        {
            case stem_master:
                if (mode == exportPatternSelection)
                {
                    sprintf(stemsPaths[stemIdx], "%s", path);
                }
                else
                {
                    sprintf(stemsPaths[stemIdx], "%s/master.wav", path);
                }
                break;
            case stem_delay: sprintf(stemsPaths[stemIdx], "%s/delay.wav", path); break;
            case stem_reverb: sprintf(stemsPaths[stemIdx], "%s/reverb.wav", path); break;

#ifdef FEATURE_SYNTH
            case stem_synth_1:
            case stem_synth_2:
            case stem_synth_3:
                sprintf(stemsPaths[stemIdx], "%s/synth_%d.wav", path, stemIdx - stem_synth_1 + 1);
                break;
#endif

            default: sprintf(stemsPaths[stemIdx], "%s/track%d.wav", path, stemIdx + 1); break;
        }

        if (EXPORT_DEBUG)
        {
            LOGF("Export setup: stem no: %d\tfile path: %s \n", stemIdx, stemsPaths[stemIdx]);
        }

        bytesWrittenInStem[stemIdx] = 0;

        engine.getExportQueues()
          ->getQueue(ExportQueues::ExportChannels::kLeft, (mtAudioExport::enStem)(stemIdx))
          ->begin();
        engine.getExportQueues()
          ->getQueue(ExportQueues::ExportChannels::kRight, (mtAudioExport::enStem)(stemIdx))
          ->begin();
    }

    if (!exportOnlyMaster())
    {
        sprintf(stemsRawPath, "%s/rawFile", path);

        if (fileSystem->exists(stemsRawPath))
            fileSystem->remove(stemsRawPath);

        auto [isOpen, fileHandle] = fileSystem->openFile(
          stemsRawPath, FileSystem::FileOpenMode::kWriteCreateNew, &rawStemsFile);

        if (!isOpen)
        {
            if (EXPORT_DEBUG)
            {
                LOGF("%s", "rawStemsFile failed to open\n");
            }

            handleError(1);
            return;
        }
    }

    runSequence();

    multipacketsInRawTotal = 0;
    lastMultipacketBlocksCount = 0;
    status = statusRender;

    if (EXPORT_DEBUG)
    {
        LOGF("%s", "Export started!\n");
    }
}

void mtAudioExport::handleError(uint16_t)
{
    cancel();
    status = statusIdle;
}

void mtAudioExport::runSequence()
{
    float tempFloat = sequencer.getTimerPeriod_us();
    seqTimerPeriod = tempFloat;
    seqTimerLastExecutionTime = 0;

    songTimeCounter = 0;

    switch (selectedMode)
    {
        case exportPatternStems:
        case exportPattern: sequencer.playPattern(); break;
        case exportSongStems:
        case exportSong:

            songLength = 0;
            while (mtProject.song.playlist[songLength] && songLength < SONG_MAX)
                songLength++;

            sequencer.playSongFromPosition(0);

            break;

        case exportPatternSelection:
            sendSelection();
            sequencer.playSelection();
            break;
        default: sequencer.playPattern(); break;
    }
}

bool mtAudioExport::isStemActive(int stemIdx)
{
    switch (selectedMode)
    {
        case enExportType::exportSongStems:
        case enExportType::exportPatternStems:
        {
            assert(stemIdx < stem_totalCount);
            return true; // all stems are active
        }
        case enExportType::exportSong:
        case enExportType::exportPattern:
        case enExportType::exportPatternSelection:
        {
            return stemIdx == stem_master;
        }
        default: assert(false); // unknown mmode
    }
    return false;
}

bool mtAudioExport::exportOnlyMaster()
{
    return selectedMode == enExportType::exportSong ||
           selectedMode == enExportType::exportPattern ||
           selectedMode == enExportType::exportPatternSelection;
}

void mtAudioExport::saveMasterBufferToSD(uint8_t closeFile)
{
    uint16_t blocksBuffered = bufferedBlocks[stem_master];
    if (blocksBuffered >= maxBlocksForStem)
        blocksBuffered = maxBlocksForStem;

    // Only the first flush may create the file. Creating it again truncates it, and what
    // the earlier flushes wrote then depends on the card handing out the same clusters.
    const FileSystem::FileOpenMode openMode = (bytesWrittenInStem[stem_master] == 0)
                                                ? FileSystem::FileOpenMode::kWriteCreateNew
                                                : FileSystem::FileOpenMode::kWriteOpen;

    [[maybe_unused]] auto [isOpen, fileHandle] =
      fileSystem->openFile(stemsPaths[stem_master], openMode, &wavStemsFile);

    if (blocksBuffered > 0)
    {
        if (fileSystem->isFileOpen(wavStemsFile))
        {
            fileSystem->seekFile(bytesWrittenInStem[stem_master] + sizeof(strWavFileHeader),
                                 wavStemsFile);

            uint8_t* ptrWrite;
            ptrWrite = (uint8_t*)renderBuffer;

            int32_t written = fileSystem->writeFile(
              ptrWrite, AUDIO_BLOCK_SAMPLES * 2 * 2 * blocksBuffered, wavStemsFile);

            if (written < 0)
            {
                handleError(2);
                return;
            }
            bytesWrittenInStem[stem_master] += static_cast<uint32_t>(written);
        }
        else
        {
            handleError(2);
        }
    }

    if (closeFile)
    {
        strWavFileHeader head;
        head.chunkId = 0x46464952; // "RIFF"
        head.chunkSize = bytesWrittenInStem[stem_master] + 36;
        head.waveId = WAVEID;          // "WAVE"
        head.subchunk1Id = 0x20746d66; // "fmt "
        head.subchunk1Size = 16;
        head.AudioFormat = 1;
        head.numChannels = 2;
        head.sampleRate = 44100;
        head.bitsPerSample = 16;
        head.byteRate = head.sampleRate * head.numChannels * (head.bitsPerSample / 8);
        head.blockAlign = head.numChannels * (head.bitsPerSample / 8);
        head.subchunk2Id = 0x61746164; // "data"
        head.subchunk2Size = bytesWrittenInStem[stem_master];

        fileSystem->seekFile(0, wavStemsFile);
        fileSystem->writeFile(&head, sizeof(strWavFileHeader), wavStemsFile);

        fileSystem->seekFile(bytesWrittenInStem[stem_master] + sizeof(strWavFileHeader),
                             wavStemsFile);
    }

    fileSystem->closeFile(wavStemsFile, false);
}

void mtAudioExport::saveRawStemsBufferToSD(uint8_t closeFile)
{
    uint16_t blocksBuffered = std::min(bufferedBlocks[stem_master], maxBlocksForStem);

    if (blocksBuffered > 0)
    {
        // zerowanie niewykorzystanego miejsca w ostatnim buforze
        for (int stem = 0; stem < stem_totalCount; stem++)
        {
            if (bufferedBlocks[stem] < maxBlocksForStem)
            {
                int16_t* writeBuff = renderBuffer[stem][bufferedBlocks[stem]];
                int dataLen = (bufferedBlocks[stem] * sizeofAudioBlock);

                memset(writeBuff, 0, sizeofPacket - dataLen);
            }
        }

        // do zaspisu caly bufor
        uint8_t* ptrWrite = (uint8_t*)renderBuffer[0][0];

        fileSystem->writeFile(ptrWrite, numOfBlocksInMultiblock * sizeofAudioBlock, rawStemsFile);

        multipacketsInRawTotal++;
        lastMultipacketBlocksCount = blocksBuffered;
    }

    if (closeFile)
    {
        fileSystem->closeFile(rawStemsFile, false);
    }
}

bool mtAudioExport::isBufferSilent(const int16_t* buffer)
{
    static constexpr int32_t kSilenceThreshold = 8;
    int16_t minSample = buffer[0];
    int16_t maxSample = buffer[0];

    for (int idx = 1; idx < AUDIO_BLOCK_SAMPLES; idx++)
    {
        minSample = std::min(minSample, buffer[idx]);
        maxSample = std::max(maxSample, buffer[idx]);
        if ((int32_t)maxSample - (int32_t)minSample > kSilenceThreshold)
        {
            return false;
        }
    }

    return true;
}

void mtAudioExport::update()
{
    switch (status)
    {
        case statusIdle: break;

        case statusRender: updateRender(); break;

        case statusRawToWavs_init: rawToWavs_init(); break;
        case statusRawToWavs_startWav: rawToWavs_startWav(); break;

        case statusRawToWavs_update: rawToWavs_update(); break;

        case statusRawToWavs_finish: rawToWavs_finish(); break;

        default: break;
    }
}
void mtAudioExport::rawToWavs_init()
{
    raw.stemIndex = 0;
    raw.currentMultipacket = 0;
    raw.totalPacketProcessed = 0;

    [[maybe_unused]] auto [isOpen, fileHandle] =
      fileSystem->openFile(stemsRawPath, FileSystem::FileOpenMode::kRead, &rawStemsFile);

    status = statusRawToWavs_startWav;
}

void mtAudioExport::rawToWavs_startWav()
{

    if (raw.stemIndex < stem_totalCount)
    {
        // plik wav docelowy
        [[maybe_unused]] auto [isOpen, fileHandle] = fileSystem->openFile(
          stemsPaths[raw.stemIndex], FileSystem::FileOpenMode::kWriteCreateNew, &wavStemsFile);

        fileSystem->writeFile((void*)renderBuffer, 44, wavStemsFile);

        status = statusRawToWavs_update;
    }
    else
    {
        handleError(1);
    }
}
void mtAudioExport::rawToWavs_update()
{
    uint8_t* buff = (uint8_t*)renderBuffer;

    if (raw.currentMultipacket < multipacketsInRawTotal)
    {
        fileSystem->seekFile(raw.currentMultipacket * sizeofMultipacket + // najpierw multipacket
                               raw.stemIndex * sizeofPacket,              // +stem offst
                             rawStemsFile);

        fileSystem->readFile(buff, sizeofPacket, rawStemsFile);

        uint32_t bytesToWrite = (raw.currentMultipacket == multipacketsInRawTotal - 1)
                                  ? (lastMultipacketBlocksCount * sizeofAudioBlock)
                                  : sizeofPacket;
        fileSystem->writeFile(buff, bytesToWrite, wavStemsFile);

        raw.currentMultipacket++;
        raw.totalPacketProcessed++;
    }

    if (raw.currentMultipacket >= multipacketsInRawTotal)
    {
        uint32_t stemDataBytes = (multipacketsInRawTotal > 0)
                                   ? ((multipacketsInRawTotal - 1) * sizeofPacket +
                                      lastMultipacketBlocksCount * sizeofAudioBlock)
                                   : 0;

        strWavFileHeader head;

        head.chunkId = 0x46464952;
        head.chunkSize = stemDataBytes + 36;
        head.waveId = WAVEID;          // "WAVE"
        head.subchunk1Id = 0x20746d66; // "fmt "
        head.subchunk1Size = 16;
        head.AudioFormat = 1;
        head.numChannels = 2;
        head.sampleRate = 44100;
        head.bitsPerSample = 16;
        head.byteRate = head.sampleRate * head.numChannels * (head.bitsPerSample / 8);
        head.blockAlign = head.numChannels * (head.bitsPerSample / 8);
        head.subchunk2Id = 0x61746164; // "data"
        head.subchunk2Size = stemDataBytes;

        fileSystem->seekFile(0, wavStemsFile);
        fileSystem->writeFile(&head, sizeof(strWavFileHeader), wavStemsFile);
        fileSystem->closeFile(wavStemsFile, false);

        raw.stemIndex++;
        if (raw.stemIndex >= stem_totalCount)
        {
            status = statusRawToWavs_finish;
        }
        else
        {
            raw.currentMultipacket = 0;
            status = statusRawToWavs_startWav;
        }
    }
}
void mtAudioExport::rawToWavs_finish()
{
    fileSystem->closeFile(rawStemsFile, false);

    if (!EXPORT_DEBUG)
    {
        if (fileSystem->exists(stemsRawPath))
            fileSystem->remove(stemsRawPath);
    }

    [[maybe_unused]] uint32_t totalExportTime_ms = totalExportTimer;
    [[maybe_unused]] uint32_t totalSongTime_ms = songTimeCounter;

    if (EXPORT_DEBUG)
        LOGF(
          "total export %ds track took %ds\n", totalSongTime_ms / 1000, totalExportTime_ms / 1000);

    callBackExternalSettings();

    status = statusIdle;
}

void mtAudioExport::handleStem(uint8_t idx)
{
    if (!isStemActive((enStem)idx))
        return;

    AudioRecordQueue* queue[ExportQueues::ExportChannels::kChannelsCount];

    queue[ExportQueues::ExportChannels::kLeft] = engine.getExportQueues()->getQueue(
      ExportQueues::ExportChannels::kLeft, (mtAudioExport::enStem)(idx));

    queue[ExportQueues::ExportChannels::kRight] = engine.getExportQueues()->getQueue(
      ExportQueues::ExportChannels::kRight, (mtAudioExport::enStem)(idx));

    if (queue[ExportQueues::ExportChannels::kLeft]->available() &&
        queue[ExportQueues::ExportChannels::kRight]->available())
    {
        const int16_t* srcL = queue[ExportQueues::ExportChannels::kLeft]->readBuffer();
        const int16_t* srcR = queue[ExportQueues::ExportChannels::kRight]->readBuffer();

        int16_t* writeBuff;
        if (exportOnlyMaster())
            writeBuff =
              ((int16_t*)renderBuffer) + (bufferedBlocks[idx] * (2 * AUDIO_BLOCK_SAMPLES));
        else
            writeBuff = renderBuffer[idx][bufferedBlocks[idx]];
        //
        for (int a = 0; a < AUDIO_BLOCK_SAMPLES; a++)
        {
            writeBuff[a * 2] = *(srcL + a);
            writeBuff[a * 2 + 1] = *(srcR + a);
        }

        bufferedBlocks[idx]++;

        if (idx == maxStemInCurrentExport && bufferedBlocks[idx] >= maxBlocksForStem)
        {
            // zrzut calego bufora na SD
            if (exportOnlyMaster())
                saveMasterBufferToSD(0);
            else
                saveRawStemsBufferToSD(0);

            for (uint8_t tr = 0; tr < stem_totalCount; tr++)
            {
                bufferedBlocks[tr] = 0;
            }
        }

        if (lastSequenceStep && idx == maxStemInCurrentExport)
        {
            assert(maxStemInCurrentExport == stem_master);
            buffersAfterStop++;

            if (selectedMode == exportPatternSelection)
            {
                finishRender();
            }
            else if (isBufferSilent(srcL) && isBufferSilent(srcR))
            {
                {
                    int delayTimeMs = engine.getDelayTime();
                    int delayTimeBuffers =
                      (int)((delayTimeMs * (kAudioSampleRate / 1000.0f)) / AUDIO_BLOCK_SAMPLES) + 1;

                    static constexpr int kReverbSilenceMaxMs = 300;
                    int reverbTimeBuffers =
                      (int)((kReverbSilenceMaxMs * (kAudioSampleRate / 1000.0f)) /
                            AUDIO_BLOCK_SAMPLES) +
                      1;

                    bool delaySilent = engine.getMixerAndFx()->getDelay()->isSilent();
                    fxTimeBuffers =
                      delaySilent ? reverbTimeBuffers : std::max(delayTimeBuffers, reverbTimeBuffers);
                }

                consecutiveEmptyBuffers++;
                if (consecutiveEmptyBuffers >= fxTimeBuffers)
                {
                    lastSequenceStep = false;
                    finishRender();
                }
            }
            else
            {
                consecutiveEmptyBuffers = 0;

                if (buffersAfterStop >= kMaxBuffersAfterStop)
                {
                    lastSequenceStep = false;
                    finishRender();
                }
            }
        }

        queue[ExportQueues::ExportChannels::kLeft]->freeBuffer();
        queue[ExportQueues::ExportChannels::kRight]->freeBuffer();
    }
    else
    {
        int16_t* writeBuff;
        if (exportOnlyMaster())
            writeBuff =
              ((int16_t*)renderBuffer) + (bufferedBlocks[idx] * (2 * AUDIO_BLOCK_SAMPLES));
        else
            writeBuff = renderBuffer[idx][bufferedBlocks[idx]];

        memset(writeBuff, 0, sizeofAudioBlock);

        bufferedBlocks[idx]++;
        if (idx == maxStemInCurrentExport && bufferedBlocks[idx] >= maxBlocksForStem)
        {
            // zrzut calego bufora na SD
            if (exportOnlyMaster())
                saveMasterBufferToSD(0);
            else
                saveRawStemsBufferToSD(0);

            for (uint8_t tr = 0; tr < stem_totalCount; tr++)
            {
                bufferedBlocks[tr] = 0;
            }
        }
        if (lastSequenceStep && idx == maxStemInCurrentExport)
        {
            lastSequenceStep = false;
            finishRender();
        }
    }
}

void mtAudioExport::updateRender()
{
    static constexpr float timeToProcessDuringUpdate_us = 200000;

    float timeProcessed_us = 0.0f;
    float oneBufferCycleTime_s = AUDIO_BLOCK_SAMPLES / kAudioSampleRate;

    while (timeProcessed_us < timeToProcessDuringUpdate_us && !renderFinished)
    {
        sequencer.tick(oneBufferCycleTime_s);
        newFileManager.update();
        voiceStreamManager.updateSynchronous();

        engine.update();
        artificial_isr();

        if (exportOnlyMaster())
        {
            handleStem(stem_master);
        }
        else
        {
            for (uint8_t tr = 0; tr < stem_totalCount; tr++)
            {
                handleStem(tr);
            }
        }

        // @shuler todo: millis == micros???
        elapsedMillisAudio::virtualMicros += oneBufferCycleTime_s * 1000000;
        elapsedMicrosAudio::virtualMicros += oneBufferCycleTime_s * 1000000;

        timeProcessed_us += oneBufferCycleTime_s * 1000000;
    }
}

void mtAudioExport::cancel()
{
    sequencer.stop();
    status = statusIdle;

    for (uint8_t tr = 0; tr < stem_totalCount; tr++)
    {
        if (fileSystem->exists(stemsPaths[tr]))
            fileSystem->remove(stemsPaths[tr]);
    }
    if (fileSystem->exists(stemsRawPath))
        fileSystem->remove(stemsRawPath);

    callBackExternalSettings();
}

uint8_t mtAudioExport::getStatus()
{
    return (uint8_t)status;
}

float mtAudioExport::getRawToWavProgress()
{

    /*
     * multipacketsInRawTotal
     * raw.currentMultipacket
     * raw.stemIndex
     */

    float totalPacketsToSave = multipacketsInRawTotal * stem_totalCount;

    return ((float)raw.totalPacketProcessed * 100) / totalPacketsToSave;
}

uint8_t mtAudioExport::getProgress()
{

    switch (selectedMode)
    {
        case exportPatternStems:
        case exportPattern:
        {
            float renderProgress =
              (sequencer.getActualPos() * 100) / (sequencer.getPatternLength() - 1);

            if (exportOnlyMaster())
            {
                return renderProgress;
            }
            else
            {
                if (status <= statusRender)
                    return mapClamped<int>(renderProgress, 0, 100, 0, 50);
                else
                    return mapClamped<int>(getRawToWavProgress(), 0, 100, 50, 100);
            }
            break;
        }
        case exportSongStems:
        case exportSong:
        {
            float renderProgress =
              mapClamped<int>(sequencer.getActualPos(),
                              0,
                              sequencer.getPatternLength() - 1,
                              (mtProject.song.playlistPos * 100) / (songLength),
                              ((mtProject.song.playlistPos + 1) * 100) / (songLength));
            if (exportOnlyMaster())
            {
                return renderProgress;
            }
            else
            {
                if (status <= statusRender)
                    return mapClamped<int>(renderProgress, 0, 100, 0, 50);
                else
                    return mapClamped<int>(getRawToWavProgress(), 0, 100, 50, 100);
            }
            break;
        }

        case exportPatternSelection:
            return (!lastSequenceStep) ? sequencer.getPlaySelectionProgress() : 100;

            break;
        default:
            return (sequencer.getActualPos() * 100) / (sequencer.getPatternLength() - 1);
            break;
    }
}
uint32_t mtAudioExport::getRenderLength()
{
    return bytesWrittenInStem[stem_master];
}
