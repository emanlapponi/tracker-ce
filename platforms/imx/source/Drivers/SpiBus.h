#ifndef SOURCE_DRIVERS_SPIDRIVER_H_
#define SOURCE_DRIVERS_SPIDRIVER_H_

#include "fsl_lpspi.h"
#include "stdio.h"

class SpiBus
{
  public:
    static constexpr int kCphaLeadingEdge = 0;
    static constexpr int kCphaTrailingEdge = 1;

    void init(LPSPI_Type* spiBase, int pcs = 0, int cpha = kCphaLeadingEdge);
    void setSpeed(uint32_t baudRate);
    uint8_t transferByte(uint8_t data, int pcs = 0, bool pcsContinous = true);
    uint32_t transfer4Bytes(uint32_t data);
    // Sends 32-bit words, least significant byte first, without reading
    // anything back. Blocks until the last bit is out.
    void writeWords(const uint32_t* data, uint32_t count);
    // The same by DMA: returns at once, the words go out in the background.
    // `data` must stay unchanged until isWriteDone() returns true, and the
    // bus must not be used for anything else. Returns false without sending
    // if the transfer cannot be done this way.
    bool startWriteWords(const uint32_t* data, uint32_t count);
    bool isWriteDone();
    uint32_t writeFailures = 0;

  private:
    static constexpr uint32_t kDefaultBaudRate = 1000000;
    LPSPI_Type* spiBase;
    uint32_t baudRate;
    lpspi_which_pcs_t pcs;
    lpspi_clock_phase_t cpha;

    void start();
    void waitForFifoSpace(uint32_t fifoSize);
    void waitUntilIdle();

    bool writeInBackground = false;
    uint32_t writeSavedTcr = 0;
    uint32_t writeSavedFcr = 0;
};

extern SpiBus spiBus4;

#endif /* SOURCE_DRIVERS_SPIDRIVER_H_ */
