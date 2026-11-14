#include "FT812.h"
#include "TrackerImpl.h"
#include "display.h"

static const uint8_t bytesPerPixel = 4;
static const uint32_t bmpWidth = 800;
static const uint32_t bmpHeight = 480;
static const uint32_t fileHeaderSize = 14;
static const uint32_t infoHeaderSize = 40; // 56
static const uint32_t bmpDataSize = bmpWidth * bmpHeight * bytesPerPixel;
static const uint32_t fileSize = fileHeaderSize + infoHeaderSize + bmpDataSize;
static constexpr uint32_t kSnapshotRamAddress = 150000;

// clang-format off

static const unsigned char fileHeader[] = {
    'B',
    'M', /// signature
    (uint8_t)(fileSize),
    (uint8_t)(fileSize >> 8),
    (uint8_t)(fileSize >> 16),
    (uint8_t)(fileSize >> 24), /// image file size in bytes
    0, 0, 0, 0, /// reserved
    (uint8_t)(fileHeaderSize + infoHeaderSize),  0, 0, 0, /// start of pixel array
};

static const unsigned char infoHeader[] = {
    (uint8_t)(infoHeaderSize),
    0,
    0,
    0, /// header size
    (uint8_t)(bmpWidth),
    (uint8_t)(bmpWidth >> 8),
    (uint8_t)(bmpWidth >> 16),
    (uint8_t)(bmpWidth >> 24), /// image width
    (uint8_t)(bmpHeight),
    (uint8_t)(bmpHeight >> 8),
    (uint8_t)(bmpHeight >> 16),
    (uint8_t)(bmpHeight >> 24), /// image height
    1, 0, /// number of color planes
    bytesPerPixel * 8, 0, /// bits per pixel
    0, 0, 0, 0, /// compression //(3)
    0, 0, 0, 0, /// image size
	0x13, 0x0B, 0, 0, /// horizontal resolution
	0x13, 0x0B, 0, 0, /// vertical resolution
    0, 0, 0, 0, /// colors in color table
    0, 0, 0, 0, /// important colors
/*
    0, 0, 255, 0, /// r
    0, 255, 0, 0, /// g
    255, 0, 0, 0, /// b
    0, 0, 0, 255, /// a
*/
};

// clang-format on

extern uint8_t lcdPclk;
extern uint8_t lcdRotate;
void cDisplay::doScreenShot()
{
    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

    if (!fileSystem->exists("Snapshots"))
        fileSystem->mkdir(0, "Snapshots");

    char bmpName[32];
    uint16_t num = 1;
    sprintf(bmpName, "Snapshots/Snapshot_%03d.bmp", 1);
    while (fileSystem->exists(bmpName))
    {
        num++;
        sprintf(bmpName, "Snapshots/Snapshot_%03d.bmp", num);
    }

    auto [isOpen, bmpHandle] =
      fileSystem->openFile(bmpName, FileSystem::FileOpenMode::kWriteCreateNew);

    fileSystem->writeFile(fileHeader, fileHeaderSize, bmpHandle);
    fileSystem->writeFile(infoHeader, infoHeaderSize, bmpHandle);

    uint8_t buf[3200];
    uint16_t readstep = 0;

    waitForChip();

    displayDriver->beginCoProList();
    API_CMD_SNAPSHOT2(0x20, kSnapshotRamAddress, 0, 240, 1600, 240);
    displayDriver->endCoProList();
    displayDriver->awaitCoProEmpty();

    readstep = 0;

    while (readstep < 240)
    {
        displayDriver->readDataRAMG(
          buf, 3200, (kSnapshotRamAddress + (3200 * 240) - 3200) - readstep * 3200);
        fileSystem->writeFile(buf, 3200, bmpHandle);
        readstep++;
    }

    displayDriver->beginCoProList();
    API_CMD_SNAPSHOT2(0x20, kSnapshotRamAddress, 0, 0, 1600, 240);
    displayDriver->endCoProList();
    displayDriver->awaitCoProEmpty();

    readstep = 0;

    while (readstep < 242)
    {
        displayDriver->readDataRAMG(
          buf, 3200, (kSnapshotRamAddress + (3200 * 240) - 3200) - readstep * 3200);
        fileSystem->writeFile(buf, 3200, bmpHandle);
        readstep++;
    }

    fileSystem->closeFile(bmpHandle);
}

void cDisplay::scheduleScreenShot(int timeSec)
{
    screenShotscheduled = true;
    screenShotTimer = timer->millis() + timeSec * 1000;
}
