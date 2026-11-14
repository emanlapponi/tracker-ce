#include "FT812.h"
#include "SpiBus.h"
#include "TimeControl.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"

#include <stdio.h>

#define FT81x_ACTIVE 0x00
#define FT81x_CLKEXT 0x44

uint8_t ftPdPin = 0;
uint8_t ftCsPin = 0;
GPIO_Type* ftPdGpio;
GPIO_Type* ftCsGpio;

uint32_t ramDisplayList = RAM_DL;
uint32_t ramCommandBuffer = RAM_CMD;

uint16_t FT81x_GPIO_DAT;
uint16_t FT81x_GPIO_DIR;

static SpiBus* spiBus = nullptr;

int FT812_Init(GPIO_Type* pdGpio,
               int pdPin,
               GPIO_Type* csGpio,
               int csPin,
               GPIO_Type* resetGpio,
               int resetPin,
               SpiBus* spiBus,
               const Ft81xConfig* config,
               bool drivePwm)
{
    ftPdPin = pdPin;
    ftCsPin = csPin;

    ftPdGpio = pdGpio;
    ftCsGpio = csGpio;

    GPIO_PinWrite(resetGpio, resetPin, 1);
    MCU_CShigh();
    MCU_PDhigh();

    ::spiBus = spiBus;
    spiBus->setSpeed(1000000);

    // ----------------------- Cycle PD pin to reset device --------------------

    MCU_Delay_20ms();
    MCU_PDlow(); // PD low to reset device
    MCU_Delay_20ms();
    MCU_PDhigh(); // PD high again
    MCU_Delay_20ms();
    MCU_Delay_20ms();

    // ---------------------- extended configuration --------------------
    /*
        EVE_CmdWrite(0x42,0);
        delay(1);
        EVE_CmdWrite(0x61,0);
        delay(1);
        EVE_CmdWrite(0x00,0);
        delay(1);
        //EVE_CmdWrite(0x44,0);
        delay(1);
        EVE_CmdWrite(0x49,0);
        delay(1);
        EVE_CmdWrite(0x68,0);
        delay(1);
    */

    EVE_CmdWrite(FT81x_ACTIVE, 0x00); // Sends 00 00 00 to wake FT8xx

    // --------------- Check that FT8xx ready and SPI communication -----------------
    uint32_t timeout = 0;
    constexpr int kReadTimeoutMs = 1000;

    // Read REG_ID register until reads 0x7C
    while (EVE_MemRead8(REG_ID) != 0x7C)
    {
        timeout++;
        if (timeout >= kReadTimeoutMs)
        {
            return 1;
        }
        delay(1);
    }

    timeout = 0;
    // Ensure CPUreset register reads 0 and so FT8xx is ready
    while (EVE_MemRead8(REG_CPURESET) != 0x00)
    {
        timeout++;
        if (timeout >= kReadTimeoutMs)
        {
            return 2;
        }
        delay(1);
    }

    // ---------------- CLOCK  --------------------
    EVE_MemWrite8(REG_PCLK, 0); // stop clocking data to the LCD panel

    // ---------------- Configure the GPIO and PWM  --------------------

    EVE_MemWrite8(REG_PWM_DUTY, 0); // Backlight off

    FT81x_GPIO_DAT =
      EVE_MemRead16(REG_GPIOX); // Read the FT81x GPIO register for a read/modify/write operation
    FT81x_GPIO_DAT = FT81x_GPIO_DAT | 0x8000; // set bit 15 of GPIO register (DISP). If using audio,
                                              // also enable the amplifier shutdown GPIO line here
    EVE_MemWrite16(REG_GPIOX, FT81x_GPIO_DAT); // Enable the DISP signal to the LCD panel

    FT81x_GPIO_DIR = EVE_MemRead16(
      REG_GPIOX_DIR); // Read the FT81x GPIO DIR register for a read/modify/write operation
    FT81x_GPIO_DIR =
      FT81x_GPIO_DIR | 0x8000; // DISP is output, if using audio, also set the associated shutdown
                               // line as output (check your Eval module schematic))
    EVE_MemWrite16(REG_GPIOX_DIR, FT81x_GPIO_DIR); // Enable the DISP signal to the LCD panel

    EVE_MemWrite16(REG_HSIZE, config->lcdWidth);
    EVE_MemWrite16(REG_HCYCLE, config->lcdHcycle);
    EVE_MemWrite16(REG_HOFFSET, config->lcdHoffset);
    EVE_MemWrite16(REG_HSYNC0, config->lcdHsync0);
    EVE_MemWrite16(REG_HSYNC1, config->lcdHsync1);
    EVE_MemWrite16(REG_VSIZE, config->lcdHeight);
    EVE_MemWrite16(REG_VCYCLE, config->lcdVcycle);
    EVE_MemWrite16(REG_VOFFSET, config->lcdVoffset);
    EVE_MemWrite16(REG_VSYNC0, config->lcdVsync0);
    EVE_MemWrite16(REG_VSYNC1, config->lcdVsync1);

    EVE_MemWrite8(REG_SWIZZLE, config->lcdSwizzle);
    EVE_MemWrite8(REG_PCLK_POL, config->lcdPclkpol);
    EVE_MemWrite8(REG_CSPREAD, config->lcdCsSpread);
    EVE_MemWrite8(REG_ROTATE, config->lcdRotate);
    EVE_MemWrite8(REG_DITHER, config->lcdDither);
    EVE_MemWrite16(REG_OUTBITS, config->outBits);

    // ---------------------- Create an initial screen before we enable the display
    // -------------------------

    ramDisplayList = RAM_DL; // start of Display List
    EVE_MemWrite32(ramDisplayList,
                   0x02000000); // Clear Color RGB sets the colour to clear screen to

    ramDisplayList += 4; // point to next location
    EVE_MemWrite32(ramDisplayList,
                   (0x26000000 | 0x00000007)); // Clear 00100110 -------- -------- -----CST  (C/S/T
                                               // define which parameters to clear)

    ramDisplayList += 4; // point to next location
    EVE_MemWrite32(
      ramDisplayList,
      0x00000000); // DISPLAY command 00000000 00000000 00000000 00000000 (end of display list)

    EVE_MemWrite32(REG_DLSWAP, DLSWAP_FRAME); // Swap display list to make the edited one active

    // ---------------------- Touch and Audio settings -------------------------

    EVE_MemWrite16(REG_TOUCH_RZTHRESH, 1200); // Eliminate any false touches

    EVE_MemWrite8(REG_VOL_PB, 0);      // turn recorded audio volume down
    EVE_MemWrite8(REG_VOL_SOUND, 0);   // turn volume down
    EVE_MemWrite16(REG_SOUND, 0x6000); // set volume mute

    // -------------------- Now turn on PCLK and ramp the PWM up
    // -------------------------------------
    // ---------------- This provides a clean start-up for the application
    // ----------------------------

    EVE_MemWrite8(REG_PCLK, config->lcdPclk); // Now start clocking data to the LCD panel

    EVE_MemWrite16(REG_PWM_HZ, 250); // Backlight pwm 250hZ

    if (drivePwm)
    {
        for (int pwm = 0; pwm <= 128; pwm++)
        {
            EVE_MemWrite8(REG_PWM_DUTY, pwm);
            delayMicroseconds(3);
        }
    }

    // Set fast SPI settings for further communication with controller
    FT812_SetFastTransferMode();

    return 0;
}

void FT812_SetFastTransferMode()
{
    spiBus->setSpeed(30000000);
}

static bool backgroundWrite = false;

void MCU_BackgroundWriteStarted(void)
{
    backgroundWrite = true;
}

// Returns true when no write is going on in the background. Ends the SPI
// transaction of one that has just finished.
bool MCU_BackgroundWritePoll(void)
{
    if (!backgroundWrite)
    {
        return true;
    }
    if (!spiBus->isWriteDone())
    {
        return false;
    }
    backgroundWrite = false;
    MCU_CShigh();
    return true;
}

void MCU_CSlow(void)
{
    // Every access to the chip starts here, so this is where anything else
    // waits for a display list that is still being sent.
    while (!MCU_BackgroundWritePoll())
    {
    }
    GPIO_PinWrite(ftCsGpio, ftCsPin, 0);
    delayMicroseconds(1);
}

void MCU_CShigh(void)
{
    delayMicroseconds(1);
    GPIO_PinWrite(ftCsGpio, ftCsPin, 1);
}

void MCU_PDlow(void)
{
    GPIO_PinWrite(ftPdGpio, ftPdPin, 0);
}

void MCU_PDhigh(void)
{
    GPIO_PinWrite(ftPdGpio, ftPdPin, 1);
}

uint8_t MCU_SPIReadWrite(uint8_t DataToWrite)
{
    return spiBus->transferByte(DataToWrite);
}

void MCU_SPIWrite32(uint32_t DataToWrite)
{
    spiBus->transfer4Bytes(DataToWrite);
}

void MCU_Delay_3us(void)
{
    delayMicroseconds(3);
}

void MCU_Delay_20ms(void)
{
    delay(20);
}

void MCU_Delay_500ms(void)
{
    delay(500);
}

void MCU_set_sleepMode(void)
{
    EVE_MemWrite8(REG_PWM_DUTY, 0);

    FT81x_GPIO_DAT = EVE_MemRead16(REG_GPIOX);
    FT81x_GPIO_DAT = FT81x_GPIO_DAT & (~0x8000); // set 0 on DISP pin
    EVE_MemWrite16(REG_GPIOX, FT81x_GPIO_DAT);
};

void MCU_set_runMode(void)
{
    EVE_MemWrite8(REG_PWM_DUTY, 64); // starting PWM
}
