#include "Si4703.h"
#include "MIMXRT1051.h"
#include "RDSParser.h"
#include "SI4703_regdefs.h"
#include "TimeControl.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "i2cBusController.h"
#include "pin_mux.h"
#include "stdint.h"
#include "string.h"

user_callback_t callback_func = nullptr;
Si4703* Si4703::instance = nullptr;
void (*interruptFunction)(void) = nullptr;

extern "C"
{
    void GPIO1_Combined_0_15_IRQHandler()
    {
        uint32_t pins_flags = GPIO_GetPinsInterruptFlags(GPIO1);

        if (pins_flags && BOARD_INITPINS_RADIO_INTERRUPT_GPIO_PIN_MASK)
        {
            if (interruptFunction != nullptr)
            {
                interruptFunction();
            }
        }

        GPIO_ClearPinsInterruptFlags(GPIO1, pins_flags);
    }
}

// przerwanie RDS
void SI4703_interrupt()
{
    Si4703::instance->rdsReadyFlag = 1;
}

// callback RDS
void stationName(char* servname)
{
    strcpy(Si4703::instance->rds_data.nazwaStacji, servname);
}

// callback RDS
void stationText(char* servname)
{
    strcpy(Si4703::instance->rds_data.textStacji, servname);
}

// callback RDS
void timeFromRDS(uint8_t hour, uint8_t minute)
{
    Si4703::instance->rds_data.godzina = hour;
    Si4703::instance->rds_data.minuta = minute;
}
// To get the Si4703 inito 2-Wire mode, SEN needs to be high and SDIO needs to be low after a reset
// The breakout board has SEN pulled high, but also has SDIO pulled high. Therefore, after a normal
// power up The Si4703 will be in an unknown state. RST must be controlled
void Si4703::init(Pins* pins)
{
    Si4703::pins = *pins;

    gpio_pin_config_t gpio_config;
    gpio_config.direction = kGPIO_DigitalOutput;
    gpio_config.outputLogic = 0;

    IOMUXC_SetPinMux(std::get<0>(pins->sdioMux),
                     std::get<1>(pins->sdioMux),
                     std::get<2>(pins->sdioMux),
                     std::get<3>(pins->sdioMux),
                     std::get<4>(pins->sdioMux),
                     0U);

    GPIO_PinInit(pins->sdioBase, pins->sdioPin, &gpio_config); // SDIO is connected to A4 for I2C
    GPIO_WritePinOutput(
      pins->sdioBase, pins->sdioPin, 0); // A low SDIO indicates a 2-Wire interface
    GPIO_WritePinOutput(pins->resetBase, pins->resetPin, 0); // Put Si4703 into reset
    delay(1); // Some delays while we allow pins to settle
    GPIO_WritePinOutput(
      pins->resetBase, pins->resetPin, 1); // Bring Si4703 out of reset with SDIO set to low and SEN
                                           // pulled high with on-board resistor
    delay(1);                              // Allow Si4703 to come out of reset

    i2cBusController->init();

    readRegisters(true); // Read the current register set

    si4703_registers[rTEST1] = 0x8100; // Enable the oscillator, from AN230 page 9, rev 0.61 (works)
    updateRegisters(true);             // Update

    delay(500); // Wait for clock to settle - from AN230 page 9
}

void Si4703::powerOn()
{
    readRegisters(true); // Read the current register set
    si4703_registers[rPOWERCFG] = (ENABLE | DMUTE | DSMUTE | RDSM | MONO);

    si4703_registers[rSYSCONFIG2] &= ~VOLUME_MASK; // Clear volume bits
    si4703_registers[rSYSCONFIG2] |= VOLUME(10);   // Set volume to 10
    si4703_registers[rSYSCONFIG2] |= SEEKTH(0x19); // Minimalne RSSI prawidlowej stacji dla seek
    si4703_registers[rSYSCONFIG3] |=
      (SKCNT(0x08) | SKSNR(0x04)); // autoseek snr i impulse detection

    updateRegisters(true); // Update

    regionHasChanged = 0; // no flag on init

    delay(110); // Max powerup time, from datasheet page 13

    readRegisters(true);
    si4703_registers[rSYSCONFIG1] |= (RDS | RDSIEN | SI4703_GPIO2(0x01)); // Enable RDS
    updateRegisters(true);

    rds.init();

    interruptFunction = SI4703_interrupt;

    instance->rds.attachServicenNameCallback(stationName);

    isInitializedProperly = 1;
}

void Si4703::powerOff()
{
    if (isInitializedProperly)
    {
        /*Procesura zgodnie z AN230*/
        readRegisters();
        si4703_registers[rSYSCONFIG1] &= ~RDS;
        updateRegisters();

        readRegisters();
        si4703_registers[rPOWERCFG] |= DISABLE;
        updateRegisters();
    }

    interruptFunction = nullptr;

    isInitializedProperly = 0;
}

void Si4703::setRegion(radio_region_t region, uint8_t init, bool ignoreErrors)
{
    readRegisters(ignoreErrors);

    si4703_registers[rSYSCONFIG2] &= ~(SPACE(0x03) | BAND(0x03)); // clear previous setting
    si4703_registers[rSYSCONFIG1] &= ~DE;

    if (init == 0)
    {
        regionHasChanged = 1;
    }

    switch (region)
    {
        case rEurope:
            freqSpacing_MHz = 0.1f;
            freqBottomBase_MHz = 87.5f;

            si4703_registers[rSYSCONFIG1] |= DE;
            si4703_registers[rSYSCONFIG2] |= (SPACE(SPACING_100kHz) | BAND(BAND_USA_EUROPE));
            break;
        case rUSA:
            freqSpacing_MHz = 0.2f;
            freqBottomBase_MHz = 87.5f;

            si4703_registers[rSYSCONFIG2] |= (SPACE(SPACING_200kHz) | BAND(BAND_USA_EUROPE));
            break;
        case rAustralia:
            freqSpacing_MHz = 0.2f;
            freqBottomBase_MHz = 87.5f;

            si4703_registers[rSYSCONFIG1] |= DE;
            si4703_registers[rSYSCONFIG2] |= (SPACE(SPACING_200kHz) | BAND(BAND_USA_EUROPE));

            break;
        case rJapan:
            freqSpacing_MHz = 0.1f;
            freqBottomBase_MHz = 76.0f;

            si4703_registers[rSYSCONFIG1] |= DE;
            si4703_registers[rSYSCONFIG2] |= (SPACE(SPACING_100kHz) | BAND(BAND_JAPAN_WIDE));
            break;
    }

    currentRegion = region;

    updateRegisters(ignoreErrors);
}

bool Si4703::setFrequency(float freq)
{
    if (isInitializedProperly && seek_control.seek_state == notInitialized)
    {
        if (freqSpacing_MHz <= 0.0f)
        {
            return false;
        }

        int maxChannel =
          (int)((MAX_RADIO_FREQ_MHZ - freqBottomBase_MHz) / freqSpacing_MHz + 0.001f);
        int newChannel =
          (int)((freq - freqBottomBase_MHz) / freqSpacing_MHz + 0.5f); // 0.5 for ceil rounding
        if (newChannel < 0)
        {
            newChannel = 0;
        }
        else if (newChannel > maxChannel)
        {
            newChannel = maxChannel;
        }

        if (readRegisters() != I2cBusController::OperationResult::kOK)
        {
            return false;
        }

        if ((si4703_registers[rSTATUSRSSI] & STC) != 0 ||
            (si4703_registers[rCHANNEL] & TUNE) != 0)
        {
            si4703_registers[rCHANNEL] &= ~TUNE;
            si4703_registers[rPOWERCFG] &= ~SEEK;
            if (updateRegisters() != I2cBusController::OperationResult::kOK)
            {
                return false;
            }

            uint32_t clearStartMs = millis();
            bool stcCleared = false;
            while ((millis() - clearStartMs) < RADIO_I2C_TIMEOUT_MS)
            {
                if (readRegisters() != I2cBusController::OperationResult::kOK)
                {
                    return false;
                }
                if ((si4703_registers[rSTATUSRSSI] & STC) == 0)
                {
                    stcCleared = true;
                    break;
                }
                delay(1);
            }

            if (!stcCleared)
            {
                return false;
            }
        }

        si4703_registers[rCHANNEL] &= ~CHANN_MASK;                // Clear out the channel bits
        si4703_registers[rCHANNEL] |= (newChannel & CHANN_MASK);  // Mask in the new channel
        si4703_registers[rCHANNEL] |= TUNE;                       // Set the TUNE bit to start
        if (updateRegisters() != I2cBusController::OperationResult::kOK)
        {
            return false;
        }

        constexpr uint32_t kTuneTimeoutMs = 150;
        uint32_t startMs = millis();
        bool stcSet = false;
        while ((millis() - startMs) < kTuneTimeoutMs)
        {
            delay(1);
            if (readRegisters() != I2cBusController::OperationResult::kOK)
            {
                return false;
            }
            if ((si4703_registers[rSTATUSRSSI] & STC) != 0)
            {
                stcSet = true;
                break; // Tuning complete!
            }
        }

        si4703_registers[rCHANNEL] &= ~TUNE; // Clear the tune after a tune has completed
        if (updateRegisters() != I2cBusController::OperationResult::kOK)
        {
            return false;
        }

        // Wait for the si4703 to clear the STC as well
        startMs = millis();
        while ((millis() - startMs) < RADIO_I2C_TIMEOUT_MS)
        {
            if (readRegisters() != I2cBusController::OperationResult::kOK)
            {
                return false;
            }
            if ((si4703_registers[rSTATUSRSSI] & STC) == 0)
            {
                return stcSet; // Tuning complete!
            }
            delay(1);
        }
    }

    return false;
}

void Si4703::seekUp()
{
    if (isInitializedProperly)
    {
        initSeek(SEEK_UP);
    }
}

void Si4703::seekDown()
{
    if (isInitializedProperly)
    {
        initSeek(SEEK_DOWN);
    }
}

void Si4703::setVolume(int volume)
{
    if (isInitializedProperly)
    {
        readRegisters();
        if (volume < 0)
            volume = 0;
        if (volume > 15)
            volume = 15;
        si4703_registers[rSYSCONFIG2] &= ~VOLUME_MASK; // Clear volume bits
        si4703_registers[rSYSCONFIG2] |= volume;       // Set new volume
        updateRegisters();
    }
}

bool Si4703::update_RDS()
{
    if (isInitializedProperly)
    {
        if (instance->rdsReadyFlag)
        {
            readRegisters();
            if (((si4703_registers[rSTATUSRSSI] & 0x0600) == 0) &&
                (si4703_registers[rSTATUSRSSI] & RDSS))
            {
                rds.processData(si4703_registers[rRDSA],
                                si4703_registers[rRDSB],
                                si4703_registers[rRDSC],
                                si4703_registers[rRDSD]);
            }

            instance->rdsReadyFlag = 0;

            return 1;
        }
    }

    return 0;
}

void Si4703::clearRDS()
{
    memset(instance->rds_data.nazwaStacji, 0, sizeof(instance->rds_data.nazwaStacji));
    memset(instance->rds_data.textStacji, 0, sizeof(instance->rds_data.textStacji));

    instance->rds_data.godzina = 0;
    instance->rds_data.minuta = 0;
}

// Read the entire register control set from 0x00 to 0x0F
uint8_t Si4703::readRegisters(bool ignoreErrors)
{
    // Si4703 begins reading from register upper register of 0x0A and reads to 0x0F, then loops to
    // 0x00.

    uint8_t data[32] = {};
    I2cBusController::OperationResult result =
      i2cBusController->receiveData(SI4703_I2C_ADDR, 0, 0, &data, sizeof(data));

    // We want to read the entire register set from 0x0A to 0x09 = 32 bytes.

    if (!ignoreErrors && result == I2cBusController::OperationResult::kError)
    {
        // Serial.println("radio i2c read error");

        i2cBusController->unblockBus();
        init(&pins);
        powerOn();
        setRegion(currentRegion, 1, true);

        // Serial.println("repeating read after reinit");

        result = i2cBusController->receiveData(SI4703_I2C_ADDR, 0, 0, &data, sizeof(data));
    }

    if (result != I2cBusController::OperationResult::kOK)
    {
        return result;
    }

    // Remember, register 0x0A comes in first so we have to shuffle the array around a bit
    int dataIdx = 0;
    for (int x = 0x0A;; x++)
    {
        if (x == 0x10)
            x = 0; // Loop back to zero
        si4703_registers[x] = data[dataIdx++] << 8;
        si4703_registers[x] |= data[dataIdx++];
        if (x == 0x09)
            break; // We're done!
    }

    return result;
}

// Write the current 9 control registers (0x02 to 0x07) to the Si4703
// It's a little weird, you don't write an I2C addres
// The Si4703 assumes you are writing to 0x02 first, then increments
uint8_t Si4703::updateRegisters(bool ignoreErrors)
{
    // Tylko rejestry od 0x02  do 0x07 sa aktualizowane, reszta to readonly
    uint8_t dataToSend[2 * (0x08 - 0x02)];

    int dataIdx = 0;
    for (int regSpot = 0x02; regSpot < 0x08; regSpot++)
    {
        dataToSend[dataIdx++] = si4703_registers[regSpot] >> 8;
        dataToSend[dataIdx++] = si4703_registers[regSpot] & 0x00FF;
    }

    I2cBusController::OperationResult result =
      i2cBusController->sendData(SI4703_I2C_ADDR, 0, 0, dataToSend, sizeof(dataToSend));

    if (!ignoreErrors && result == I2cBusController::OperationResult::kError)
    {
        // Serial.println("radio i2c write error");

        int repeat = 10;
        while (repeat)
        {
            repeat--;
            // Serial.println("repeating write");

            result =
              i2cBusController->sendData(SI4703_I2C_ADDR, 0, 0, dataToSend, sizeof(dataToSend));

            if (result != I2cBusController::OperationResult::kError)
            {
                break;
            }
        }

        if (result == I2cBusController::OperationResult::kError)
        {
            i2cBusController->unblockBus();
            init(&pins);
            powerOn();
            setRegion(currentRegion, 1, true);
        }
    }

    return result;
}

void Si4703::initSeek(uint8_t seekDirection)
{
    seek_control.seek_dir = seekDirection;

    if (seek_control.seek_state != notInitialized)
    {
        readRegisters();
        si4703_registers[rPOWERCFG] &= ~SEEK;
        updateRegisters();
        if (callback_func != NULL)
        {
            callback_func();
        }
    }

    seek_control.seek_state = seekInit;
}

void Si4703::stateMachineSeek()
{
    if (isInitializedProperly)
    {
        switch (seek_control.seek_state)
        {
            case notInitialized:
                break;
            case seekInit:
                if (readRegisters() != I2cBusController::OperationResult::kOK)
                {
                    seek_control.seek_state = notInitialized;
                    break;
                }

                si4703_registers[rPOWERCFG] &= ~SKMODE;
                if (seek_control.seek_dir == SEEK_DOWN)
                {
                    si4703_registers[rPOWERCFG] &= ~SEEKUP; // Seek down is the default upon reset
                }
                else
                {
                    si4703_registers[rPOWERCFG] |= SEEKUP; // Set the bit to seek up
                }

                si4703_registers[rPOWERCFG] |= SEEK; // Start seek
                if (updateRegisters() != I2cBusController::OperationResult::kOK)
                {
                    seek_control.seek_state = notInitialized;
                    break;
                }

                seek_control.seek_state = loopUntilSTC;

                break;

            case loopUntilSTC:

                if (readRegisters() != I2cBusController::OperationResult::kOK)
                {
                    seek_control.seek_state = notInitialized;
                    break;
                }
                if ((si4703_registers[rSTATUSRSSI] & STC) != 0)
                {
                    seek_control.seek_state = stationFound;
                }
                else if ((si4703_registers[rPOWERCFG] & SEEK) == 0)
                {
                    seek_control.seek_state = callback;
                }

                break;

            case stationFound:
                if (readRegisters() != I2cBusController::OperationResult::kOK)
                {
                    seek_control.seek_state = notInitialized;
                    break;
                }
                // valueSFBL=si4703_registers[rSTATUSRSSI] & SF_BL; //Store the value of SFBL
                si4703_registers[rPOWERCFG] &= ~SEEK; // Clear the seek bit after seek has completed
                if (updateRegisters() != I2cBusController::OperationResult::kOK)
                {
                    seek_control.seek_state = notInitialized;
                    break;
                }

                seek_control.seek_state = seekDeinit;

                break;

            case seekDeinit:
                if (readRegisters() != I2cBusController::OperationResult::kOK)
                {
                    seek_control.seek_state = notInitialized;
                    break;
                }
                if ((si4703_registers[rSTATUSRSSI] & STC) == 0)
                {
                    seek_control.seek_state = callback;
                }
                else
                {
                    si4703_registers[rPOWERCFG] &= ~SEEK;
                    if (updateRegisters() != I2cBusController::OperationResult::kOK)
                    {
                        seek_control.seek_state = notInitialized;
                    }
                }

                break;

            case callback:

                if (callback_func != nullptr)
                {
                    callback_func();
                }

                seek_control.seek_state = notInitialized;

                break;

            default:
                break;
        }
    }
}

void Si4703::setSeekCallback(user_callback_t callback)
{
    callback_func = callback;
}

void Si4703::resetSeekCallback()
{
    callback_func = NULL;
}

float Si4703::getFrequency()
{
    if (isInitializedProperly)
    {
        int channel;
        float freq;

        readRegisters();

        channel = READCHANN(si4703_registers[rREADCHAN]);

        freq = (channel * freqSpacing_MHz + freqBottomBase_MHz);

        return freq;
    }
    else
    {
        return 0;
    }
}

uint8_t Si4703::hasRegionChanged() // flaga automatycznie sie kasuje po odczytaniu
{
    uint8_t temp = regionHasChanged;

    regionHasChanged = 0;

    return temp;
}
float Si4703::getRadioSpacing()
{
    return freqSpacing_MHz;
}

float Si4703::getRadioBottomBase()
{
    return freqBottomBase_MHz;
}

int Si4703::getRSSI()
{
    if (isInitializedProperly)
    {
        readRegisters();
        return RSSI(si4703_registers[rSTATUSRSSI]);
    }

    return 0;
}
uint8_t Si4703::getInitializationStatus()
{
    return isInitializedProperly;
}
