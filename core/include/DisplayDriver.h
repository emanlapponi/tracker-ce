#ifndef INCLUDE_DISPLAYDRIVER_H_
#define INCLUDE_DISPLAYDRIVER_H_

#include <stdint.h>

class DisplayDriver
{
  public:
    virtual void setPowerState(bool state) = 0;

    virtual void setBrightness(int value) = 0;
    virtual void setRotation(int value) = 0;

    virtual void getImgProps(uint32_t& addr, uint32_t& width, uint32_t& height) = 0;
    virtual uint32_t getDisplayListOffset() = 0;
    virtual void IncCMDOffset(uint16_t value) = 0;
    virtual void WriteData32(uint32_t data) = 0;

    virtual void beginCoProList() = 0;
    virtual void beginCoProListNoCheck() = 0;
    virtual void endCoProList() = 0;
    virtual void awaitCoProEmpty() = 0;
    virtual uint8_t isCoProEmpty() = 0;
    virtual void writeDataRAMG(const uint8_t* imgData, uint32_t dataSize, uint32_t destAddress) = 0;
    virtual void writeFontRAMG(const uint8_t* imgData, uint32_t dataSize, uint32_t destAddress) = 0;
    virtual void readDataRAMG(uint8_t* imgData, uint32_t dataSize, uint32_t sourceAddress) = 0;
    virtual void readBackDataRAMG(uint8_t* imgData, uint32_t dataSize, uint32_t sourceAddress) = 0;
    virtual uint8_t sendString(const char* string, uint32_t length) = 0;
    virtual void writeDataToCMD(const uint8_t* imgData, uint32_t totalDataSize) = 0;

    // Direct access to the chip's display-list memory, bypassing the
    // coprocessor. `offset` is in bytes from the start of the list. A swap
    // takes effect at the next frame of the panel; until then the list memory
    // must not be written.
    virtual void writeDisplayList(uint32_t offset, const uint32_t* words, uint32_t count) = 0;
    // A driver that can send in the background (DMA) returns true from
    // writesDisplayListInBackground(). A write started with
    // startDisplayListWrite() then goes on after the call returns; `words`
    // must stay valid and the list must not be swapped until
    // isDisplayListWriteDone() says so. Other use of the driver in between
    // waits for the write to finish.
    virtual bool writesDisplayListInBackground() { return false; }
    virtual void startDisplayListWrite(uint32_t offset, const uint32_t* words, uint32_t count)
    {
        writeDisplayList(offset, words, count);
    }
    virtual bool isDisplayListWriteDone() { return true; }
    virtual void swapDisplayList() = 0;
    virtual bool isDisplayListSwapPending() = 0;

  protected:
    void startCoProList()
    {
        if (!coProStarted)
        {
            beginCoProListNoCheck();
            coProStarted = true;
        }
    };

    void finishCoProList()
    {
        if (coProStarted)
        {
            endCoProList();
            coProStarted = true;
        }
    }

    bool coProStarted = false;
};

#endif /* INCLUDE_DISPLAYDRIVER_H_ */
