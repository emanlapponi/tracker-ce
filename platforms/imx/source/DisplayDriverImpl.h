#ifndef INCLUDE_DISPLAYDRIVERIMPL_H_
#define INCLUDE_DISPLAYDRIVERIMPL_H_

#include "DisplayDriver.h"

class SpiBus;

class DisplayDriverImpl : public DisplayDriver
{
  public:
    virtual void setPowerState(bool state) override;

    virtual void setBrightness(int value) override;
    virtual void setRotation(int value) override;

    virtual void getImgProps(uint32_t& addr, uint32_t& width, uint32_t& height) override;

    // ft81x
    virtual uint32_t getDisplayListOffset() override;
    virtual void IncCMDOffset(uint16_t value) override;
    virtual void WriteData32(uint32_t data) override;

    virtual void beginCoProList() override;
    virtual void beginCoProListNoCheck() override;
    virtual void endCoProList() override;
    virtual void awaitCoProEmpty() override;
    virtual uint8_t isCoProEmpty() override;
    virtual void writeDataRAMG(const uint8_t* imgData,
                               uint32_t dataSize,
                               uint32_t destAddress) override;
    virtual void writeFontRAMG(const uint8_t* imgData,
                               uint32_t dataSize,
                               uint32_t destAddress) override;
    virtual void readDataRAMG(uint8_t* imgData, uint32_t dataSize, uint32_t sourceAddress) override;
    virtual void readBackDataRAMG(uint8_t* imgData,
                                  uint32_t dataSize,
                                  uint32_t sourceAddress) override;
    virtual uint8_t sendString(const char* string, uint32_t length) override;
    virtual void writeDataToCMD(const uint8_t* imgData, uint32_t totalDataSize) override;

    virtual void writeDisplayList(uint32_t offset, const uint32_t* words, uint32_t count) override;
    virtual bool writesDisplayListInBackground() override { return true; }
    virtual void startDisplayListWrite(uint32_t offset,
                                       const uint32_t* words,
                                       uint32_t count) override;
    virtual bool isDisplayListWriteDone() override;
    virtual void swapDisplayList() override;
    virtual bool isDisplayListSwapPending() override;

    void init(SpiBus* spi);

  protected:
    uint16_t cmdOffset = 0x0000;

    SpiBus* spi;
};

#endif /* INCLUDE_DISPLAYDRIVERIMPL_H_ */
