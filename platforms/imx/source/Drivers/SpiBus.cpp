#include "SpiBus.h"
#include "clock_config.h"
#include "fsl_cache.h"
#include "fsl_clock.h"
#include "fsl_dmamux.h"
#include "fsl_edma.h"
#include "fsl_lpspi.h"
#include "polyassert.h"

SpiBus spiBus4;

static constexpr uint8_t kEdmaTxChannel = 5;
// The largest frame the peripheral can send, in 32-bit words.
static constexpr uint32_t kWordsPerFrame = 128;

//----------------------------------------------------------------------------------

extern void delayMicroseconds(uint32_t valueUs);
extern void delay(uint32_t valueMs);

void SpiBus::setSpeed(uint32_t baudRate)
{
    this->baudRate = baudRate;
    start();
}

uint8_t SpiBus::transferByte(uint8_t data, int pcs, bool pcsContinous)
{
    assert(spiBase != nullptr);
    (void)pcs;
    (void)pcsContinous;

    while ((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) != 0U)
    {
    }
    while ((spiBase->FSR & LPSPI_FSR_RXCOUNT_MASK) != 0U)
    {
        (void)spiBase->RDR;
    }

    spiBase->TDR = data;

    while ((spiBase->FSR & LPSPI_FSR_RXCOUNT_MASK) == 0U)
    {
    }
    uint8_t receivedData = (uint8_t)spiBase->RDR;

    while ((spiBase->SR & LPSPI_SR_MBF_MASK) != 0U)
    {
    }

    return receivedData;
}

uint32_t SpiBus::transfer4Bytes(uint32_t data)
{
    assert(spiBase != nullptr);

    while ((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) != 0U)
    {
    }
    while ((spiBase->FSR & LPSPI_FSR_RXCOUNT_MASK) != 0U)
    {
        (void)spiBase->RDR;
    }

    spiBase->TDR = (uint8_t)(data);
    spiBase->TDR = (uint8_t)(data >> 8);
    spiBase->TDR = (uint8_t)(data >> 16);
    spiBase->TDR = (uint8_t)(data >> 24);

    for (int i = 0; i < 4; ++i)
    {
        while ((spiBase->FSR & LPSPI_FSR_RXCOUNT_MASK) == 0U)
        {
        }
        (void)spiBase->RDR;
    }

    while ((spiBase->SR & LPSPI_SR_MBF_MASK) != 0U)
    {
    }

    return 0;
}

void SpiBus::writeWords(const uint32_t* data, uint32_t count)
{
    assert(spiBase != nullptr);

    while ((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) != 0U || (spiBase->SR & LPSPI_SR_MBF_MASK) != 0U)
    {
    }

    const uint32_t fifoSize = 1UL << ((spiBase->PARAM & LPSPI_PARAM_TXFIFO_MASK) >>
                                      LPSPI_PARAM_TXFIFO_SHIFT);
    const uint32_t byteFrames = spiBase->TCR;
    // The peripheral pauses between frames, so the words go out in frames of
    // up to 128 instead of four 8-bit frames each. The receiver is masked:
    // nothing has to be drained and the FIFO can be kept full. A frame's
    // settings travel through the same FIFO as its data.
    const uint32_t wordFrames = (byteFrames & ~LPSPI_TCR_FRAMESZ_MASK) | LPSPI_TCR_BYSW_MASK |
                                LPSPI_TCR_RXMSK_MASK;

    while (count > 0)
    {
        const uint32_t words = count < kWordsPerFrame ? count : kWordsPerFrame;
        waitForFifoSpace(fifoSize);
        spiBase->TCR = wordFrames | LPSPI_TCR_FRAMESZ(words * 32 - 1);
        for (uint32_t i = 0; i < words; i++)
        {
            waitForFifoSpace(fifoSize);
            spiBase->TDR = data[i];
        }
        data += words;
        count -= words;
    }

    while ((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) != 0U || (spiBase->SR & LPSPI_SR_MBF_MASK) != 0U)
    {
    }
    spiBase->TCR = byteFrames;
    while ((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) != 0U)
    {
    }
}

namespace
{
constexpr uint32_t kMaxBackgroundFrames = 16;
// Below this the set-up costs more than sending directly.
constexpr uint32_t kMinBackgroundWords = 48;
constexpr uint32_t kCacheLine = 32;

// One pair per SPI frame: the first descriptor writes the frame's settings
// to the transmit command register, the second its words to the data
// register. Both registers feed the same FIFO, so the DMA request of the
// transmitter drives the whole chain. These live in DTC RAM, which the data
// cache does not cover.
edma_tcd_t writeTcds[2 * kMaxBackgroundFrames] __attribute__((aligned(32)));
uint32_t writeFrameCommands[kMaxBackgroundFrames];
} // namespace

bool SpiBus::startWriteWords(const uint32_t* data, uint32_t count)
{
    assert(spiBase != nullptr);
    if (count < kMinBackgroundWords || count > kWordsPerFrame * kMaxBackgroundFrames ||
        spiBase != LPSPI4 || writeInBackground)
    {
        return false;
    }

    waitUntilIdle();
    writeSavedTcr = spiBase->TCR;
    writeSavedFcr = spiBase->FCR;
    const uint32_t wordFrames = (writeSavedTcr & ~LPSPI_TCR_FRAMESZ_MASK) | LPSPI_TCR_BYSW_MASK |
                                LPSPI_TCR_RXMSK_MASK;

    edma_transfer_config_t config;
    memset(&config, 0, sizeof(config));
    config.srcTransferSize = kEDMA_TransferSize4Bytes;
    config.destTransferSize = kEDMA_TransferSize4Bytes;
    config.destOffset = 0;
    config.minorLoopBytes = 4;

    const uint32_t frames = (count + kWordsPerFrame - 1) / kWordsPerFrame;
    const uint32_t* next = data;
    uint32_t left = count;
    for (uint32_t frame = 0; frame < frames; frame++)
    {
        const uint32_t words = left < kWordsPerFrame ? left : kWordsPerFrame;
        writeFrameCommands[frame] = wordFrames | LPSPI_TCR_FRAMESZ(words * 32 - 1);

        edma_tcd_t* command = &writeTcds[2 * frame];
        edma_tcd_t* payload = &writeTcds[2 * frame + 1];
        edma_tcd_t* following = frame + 1 < frames ? &writeTcds[2 * frame + 2] : nullptr;

        config.srcAddr = (uint32_t)&writeFrameCommands[frame];
        config.srcOffset = 0;
        config.destAddr = (uint32_t)&spiBase->TCR;
        config.majorLoopCounts = 1;
        EDMA_TcdReset(command);
        EDMA_TcdSetTransferConfig(command, &config, payload);

        config.srcAddr = (uint32_t)next;
        config.srcOffset = 4;
        config.destAddr = (uint32_t)&spiBase->TDR;
        config.majorLoopCounts = words;
        EDMA_TcdReset(payload);
        // Without a following descriptor the channel's request is switched
        // off when this one completes; isWriteDone() watches for that.
        EDMA_TcdSetTransferConfig(payload, &config, following);

        next += words;
        left -= words;
    }

    // The DMA reads memory, not the cache.
    const uint32_t start = (uint32_t)data & ~(kCacheLine - 1);
    const uint32_t end = ((uint32_t)(data + count) + kCacheLine - 1) & ~(kCacheLine - 1);
    DCACHE_CleanByRange(start, end - start);

    DMAMUX_Init(DMAMUX);
    EDMA_DisableChannelRequest(DMA0, kEdmaTxChannel);
    DMAMUX_DisableChannel(DMAMUX, kEdmaTxChannel);
    DMAMUX_SetSource(DMAMUX, kEdmaTxChannel, kDmaRequestMuxLPSPI4Tx);
    DMAMUX_EnableChannel(DMAMUX, kEdmaTxChannel);
    EDMA_ClearChannelStatusFlags(
      DMA0, kEdmaTxChannel, kEDMA_DoneFlag | kEDMA_ErrorFlag | kEDMA_InterruptFlag);
    EDMA_InstallTCD(DMA0, kEdmaTxChannel, &writeTcds[0]);

    // Ask for more whenever there is room for at least four entries.
    const uint32_t fifoSize = 1UL << ((spiBase->PARAM & LPSPI_PARAM_TXFIFO_MASK) >>
                                      LPSPI_PARAM_TXFIFO_SHIFT);
    spiBase->FCR = (writeSavedFcr & ~LPSPI_FCR_TXWATER_MASK) | LPSPI_FCR_TXWATER(fifoSize - 4);

    writeInBackground = true;
    EDMA_EnableChannelRequest(DMA0, kEdmaTxChannel);
    LPSPI_EnableDMA(spiBase, (uint32_t)kLPSPI_TxDmaEnable);
    return true;
}

bool SpiBus::isWriteDone()
{
    if (!writeInBackground)
    {
        return true;
    }
    const uint32_t channelBit = 1UL << kEdmaTxChannel;
    const bool failed = (DMA0->ERR & channelBit) != 0U;
    if (!failed)
    {
        if ((DMA0->ERQ & channelBit) != 0U)
        {
            return false;
        }
        if ((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) != 0U || (spiBase->SR & LPSPI_SR_MBF_MASK) != 0U)
        {
            return false;
        }
    }

    LPSPI_DisableDMA(spiBase, (uint32_t)kLPSPI_TxDmaEnable);
    EDMA_DisableChannelRequest(DMA0, kEdmaTxChannel);
    if (failed)
    {
        // Give the bus back in working order. What reached the chip is
        // unknown; it is drawn over by the next frame.
        EDMA_ClearChannelStatusFlags(DMA0, kEdmaTxChannel, kEDMA_ErrorFlag);
        LPSPI_FlushFifo(spiBase, true, true);
        writeFailures++;
    }
    spiBase->FCR = writeSavedFcr;
    spiBase->TCR = writeSavedTcr;
    while ((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) != 0U)
    {
    }
    writeInBackground = false;
    return true;
}

void SpiBus::waitUntilIdle()
{
    while ((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) != 0U || (spiBase->SR & LPSPI_SR_MBF_MASK) != 0U)
    {
    }
}

void SpiBus::waitForFifoSpace(uint32_t fifoSize)
{
    while (((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) >> LPSPI_FSR_TXCOUNT_SHIFT) >= fifoSize)
    {
    }
}

void SpiBus::init(LPSPI_Type* spiBase, int pcs, int cpha)
{
    assert(spiBase != nullptr);

    this->spiBase = spiBase;
    baudRate = kDefaultBaudRate;
    SpiBus::pcs = (lpspi_which_pcs_t)pcs;
    SpiBus::cpha = (lpspi_clock_phase_t)cpha;
    start();
}

void SpiBus::start()
{
    assert(baudRate >= 100000 && baudRate <= 50000000);

    lpspi_master_config_t masterConfig;
    LPSPI_MasterGetDefaultConfig(&masterConfig);

    masterConfig.baudRate = baudRate;
    masterConfig.pcsToSckDelayInNanoSec = 0;
    masterConfig.lastSckToPcsDelayInNanoSec = 0;
    masterConfig.betweenTransferDelayInNanoSec = 0;
    masterConfig.whichPcs = pcs;
    masterConfig.cpha = cpha;

    uint32_t spiClock = CLOCK_GetClockRootFreq(kCLOCK_LpspiClkRoot);
    LPSPI_Enable(spiBase, false);
    LPSPI_MasterInit(spiBase, &masterConfig, spiClock);
    spiBase->TCR = (spiBase->TCR & ~(LPSPI_TCR_CONT_MASK | LPSPI_TCR_CONTC_MASK |
                                     LPSPI_TCR_RXMSK_MASK | LPSPI_TCR_TXMSK_MASK |
                                     LPSPI_TCR_PCS_MASK)) |
                   LPSPI_TCR_PCS(pcs);
    while ((spiBase->FSR & LPSPI_FSR_TXCOUNT_MASK) != 0U)
    {
    }
}
