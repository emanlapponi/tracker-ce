/*
********************************************************************************
*             FT81x simple library for PIC MCU                                 *
********************************************************************************
Copyright (c) Future Technology Devices International / BridgeTek

THIS SOFTWARE IS PROVIDED BY FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES LOSS OF USE, DATA, OR PROFITS OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

FTDI DRIVERS MAY BE USED ONLY IN CONJUNCTION WITH PRODUCTS BASED ON FTDI PARTS.

FTDI DRIVERS MAY BE DISTRIBUTED IN ANY FORM AS LONG AS LICENSE INFORMATION IS NOT MODIFIED.

IF A CUSTOM VENDOR ID AND/OR PRODUCT ID OR DESCRIPTION STRING ARE USED, IT IS THE
RESPONSIBILITY OF THE PRODUCT MANUFACTURER TO MAINTAIN ANY CHANGES AND SUBSEQUENT WHQL
RE-CERTIFICATION AS A RESULT OF MAKING THESE CHANGES.

********************************************************************************
* This code is provided as an example only and is not guaranteed by FTDI.
* FTDI accept no responsibility for any issues resulting from its use.
* The developer of the final application incorporating any parts of this
* sample project is responsible for ensuring its safe and correct operation
* and for any consequences resulting from its use.
*
* Rev       Date        Author          Comment
********************************************************************************
* 1.0       June 2017	G Brown         Initial Release
* 1.1       Feb  2018   G Brown         No changes to this file
*  *****************************************************************************
*/

// This is a guard condition so that contents of this file are not included
// more than once.
#ifndef FT812_H
#define FT812_H

#include "FT81x_Config.h"
#include "SpiBus.h"

#include <stdint.h> // for Uint8/16/32 and Int8/16/32 data types
// ######################### MCU Layer #########################################

extern const uint32_t baudrateSlow;
extern const uint32_t baudrateFast;
extern const uint32_t spiSourceClock;

#define FT_81X_ENABLE 1

int FT812_Init(GPIO_Type* pdGpio,
               int pdPin,
               GPIO_Type* csGpio,
               int csPin,
               GPIO_Type* resetGpio,
               int resetPin,
               SpiBus* spiBus,
               const Ft81xConfig* config,
               bool drivePwm);
void MCU_CSlow(void);
void MCU_CShigh(void);
void MCU_BackgroundWriteStarted(void);
bool MCU_BackgroundWritePoll(void);
void MCU_PDlow(void);
void MCU_PDhigh(void);
void MCU_Delay_3us(void);
void MCU_Delay_20ms(void);
void MCU_Delay_500ms(void);
void MCU_set_sleepMode(void);
void MCU_set_runMode(void);
void FT812_SetFastTransferMode();
uint8_t MCU_SPIReadWrite(uint8_t DataToWrite);
void MCU_SPIWrite32(uint32_t DataToWrite);

void MCU_InitSpi(uint32_t baudrate, uint32_t sourceClock);
void MCU_StopSpi();
void MCU_ChangeSpiSpeed(uint32_t baudrate, uint32_t sourceClock);

// ######################### EVE Layer #########################################
void FT812_setCMDOffsetPtr(uint16_t* cmdOffset);

void EVE_AddrForWr(uint32_t ftAddress);
void EVE_AddrForRd(uint32_t ftAddress);
void EVE_Write32(uint32_t ftData32);
void EVE_Write16(uint16_t ftData16);
void EVE_Write8(uint8_t ftData8);
uint32_t EVE_Read32(void);
uint16_t EVE_Read16(void);
uint8_t EVE_Read8(void);
void EVE_MemWrite32(uint32_t ftAddress, uint32_t ftData32);
void EVE_MemWrite16(uint32_t ftAddress, uint16_t ftData16);
void EVE_MemWrite8(uint32_t ftAddress, uint8_t ftData8);
uint32_t EVE_MemRead32(uint32_t ftAddress);
uint16_t EVE_MemRead16(uint32_t ftAddress);
uint8_t EVE_MemRead8(uint32_t ftAddress);
void EVE_CmdWrite(uint8_t EVECmd, uint8_t Param);
uint16_t EVE_IncCMDOffset(uint16_t currentOffset, uint16_t commandSize);
uint8_t EVE_WaitCmdFifoEmpty(void);
uint8_t EVE_IsCmdFifoEmpty(void);
uint32_t EVE_GetCurrentWritePointer(void);
uint16_t EVE_CheckFreeSpace(uint16_t CmdOffset);

// ######################### API Layer #########################################

// API Functions
void API_LIB_BeginCoProList(void);
void API_LIB_BeginDirectDL(void);
void API_LIB_BeginCoProListNoCheck(void);
void API_LIB_EndCoProList(void);
void API_LIB_AwaitCoProEmpty(void);
uint8_t API_LIB_IsCoProEmpty(void);
void API_LIB_WriteDataRAMG(const uint8_t* ImgData, uint32_t DataSize, uint32_t DestAddress);
void API_LIB_WriteFontRAMG(const uint8_t* ImgData, uint32_t DataSize, uint32_t DestAddress);
void API_LIB_ReadDataRAMG(uint8_t* ImgData, uint32_t DataSize, uint32_t SourceAddress);
void API_LIB_ReadBackDataRAMG(uint8_t* ImgData, uint32_t DataSize, uint32_t SourceAddress);
uint8_t API_SendString(const char* string, uint32_t length);
void API_LIB_WriteDataToCMD(const uint8_t* ImgData, uint32_t TotalDataSize);

#define DLSWAP_DONE  0UL
#define DLSWAP_FRAME 2UL
#define DLSWAP_LINE  1UL

#define ROMFONT_TABLEADDRESS 3145724UL

#define REG_ANALOG          3154284UL
#define REG_ANA_COMP        3154308UL
#define REG_BIST_EN         3154292UL
#define REG_BUSYBITS        3154152UL
#define REG_CLOCK           3153928UL
#define REG_CMDB_SPACE      3155316UL
#define REG_CMDB_WRITE      3155320UL
#define REG_CMD_DL          3154176UL
#define REG_CMD_READ        3154168UL
#define REG_CMD_WRITE       3154172UL
#define REG_CPURESET        3153952UL
#define REG_CRC             3154296UL
#define REG_CSPREAD         3154024UL
#define REG_CTOUCH_EXTENDED 3154184UL
// modified below to reflect the same as datasheet
#define REG_CTOUCH_TOUCH0_XY    3154212UL
#define REG_CTOUCH_TOUCH4_X     3154284UL
#define REG_CTOUCH_TOUCH4_Y     3154208UL
#define REG_CTOUCH_TOUCH1_XY    3154204UL
#define REG_CTOUCH_TOUCH2_XY    3154316UL
#define REG_CTOUCH_TOUCH3_XY    3154320UL
#define REG_CYA_TOUCH           3154280UL
#define REG_DATESTAMP           3155300UL
#define REG_DITHER              3154016UL
#define REG_DLSWAP              3154004UL
#define REG_FRAMES              3153924UL
#define REG_FREQUENCY           3153932UL
#define REG_GPIO                3154068UL
#define REG_GPIOX               3154076UL
#define REG_GPIOX_DIR           3154072UL
#define REG_GPIO_DIR            3154064UL
#define REG_HCYCLE              3153964UL
#define REG_HOFFSET             3153968UL
#define REG_HSIZE               3153972UL
#define REG_HSYNC0              3153976UL
#define REG_HSYNC1              3153980UL
#define REG_ID                  3153920UL
#define REG_INT_EN              3154092UL
#define REG_INT_FLAGS           3154088UL
#define REG_INT_MASK            3154096UL
#define REG_MACRO_0             3154136UL
#define REG_MACRO_1             3154140UL
#define REG_MEDIAFIFO_READ      3182612UL
#define REG_MEDIAFIFO_WRITE     3182616UL
#define REG_OUTBITS             3154012UL
#define REG_PATCHED_ANALOG      3154288UL
#define REG_PATCHED_TOUCH_FAULT 3154284UL
#define REG_PCLK                3154032UL
#define REG_PCLK_POL            3154028UL
#define REG_PLAY                3154060UL
#define REG_PLAYBACK_FORMAT     3154116UL
#define REG_PLAYBACK_FREQ       3154112UL
#define REG_PLAYBACK_LENGTH     3154104UL
#define REG_PLAYBACK_LOOP       3154120UL
#define REG_PLAYBACK_PLAY       3154124UL
#define REG_PLAYBACK_READPTR    3154108UL
#define REG_PLAYBACK_START      3154100UL
#define REG_PWM_DUTY            3154132UL
#define REG_PWM_HZ              3154128UL
#define REG_RENDERMODE          3153936UL
#define REG_ROMSUB_SEL          3154160UL
#define REG_ROTATE              3154008UL
#define REG_SNAPFORMAT          3153948UL
#define REG_SNAPSHOT            3153944UL
#define REG_SNAPY               3153940UL
#define REG_SOUND               3154056UL
#define REG_SPI_EARLY_TX        3154300UL
#define REG_SPI_WIDTH           3154312UL
#define REG_SWIZZLE             3154020UL
#define REG_TAG                 3154044UL
#define REG_TAG_X               3154036UL
#define REG_TAG_Y               3154040UL
#define REG_TAP_CRC             3153956UL
#define REG_TAP_MASK            3153960UL
#define REG_TOUCH_ADC_MODE      3154184UL
#define REG_TOUCH_CHARGE        3154188UL
#define REG_TOUCH_DIRECT_XY     3154316UL
#define REG_TOUCH_DIRECT_Z1Z2   3154320UL
#define REG_TOUCH_FAULT         3154288UL
#define REG_TOUCH_MODE          3154180UL
#define REG_TOUCH_OVERSAMPLE    3154196UL
#define REG_TOUCH_RAW_XY        3154204UL
#define REG_TOUCH_RZ            3154208UL
#define REG_TOUCH_RZTHRESH      3154200UL
#define REG_TOUCH_SCREEN_XY     3154212UL
#define REG_TOUCH_SETTLE        3154192UL
#define REG_TOUCH_TAG           3154220UL
#define REG_TOUCH_TAG1          3154228UL
#define REG_TOUCH_TAG1_XY       3154224UL
#define REG_TOUCH_TAG2          3154236UL
#define REG_TOUCH_TAG2_XY       3154232UL
#define REG_TOUCH_TAG3          3154244UL
#define REG_TOUCH_TAG3_XY       3154240UL
#define REG_TOUCH_TAG4          3154252UL
#define REG_TOUCH_TAG4_XY       3154248UL
#define REG_TOUCH_TAG_XY        3154216UL
#define REG_TOUCH_TRANSFORM_A   3154256UL
#define REG_TOUCH_TRANSFORM_B   3154260UL
#define REG_TOUCH_TRANSFORM_C   3154264UL
#define REG_TOUCH_TRANSFORM_D   3154268UL
#define REG_TOUCH_TRANSFORM_E   3154272UL
#define REG_TOUCH_TRANSFORM_F   3154276UL
#define REG_TRACKER             3182592UL
#define REG_TRACKER_1           3182596UL
#define REG_TRACKER_2           3182600UL
#define REG_TRACKER_3           3182604UL
#define REG_TRACKER_4           3182608UL
#define REG_TRIM                3154304UL
#define REG_VCYCLE              3153984UL
#define REG_VOFFSET             3153988UL
#define REG_VOL_PB              3154048UL
#define REG_VOL_SOUND           3154052UL
#define REG_VSIZE               3153992UL
#define REG_VSYNC0              3153996UL
#define REG_VSYNC1              3154000UL

#define RAM_CMD    3178496UL
#define RAM_DL     3145728UL
#define RAM_G      0UL
#define RAM_REG    3153920UL
#define RAM_ROMSUB 3186688UL

#endif
