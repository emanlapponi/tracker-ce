#ifndef SparkFunSi4703_h
#define SparkFunSi4703_h

#include "Arduino.h"
#include "RDSParser.h"

#define SI4703_I2C_ADDR 0x10
#define RADIO_I2C_TIMEOUT_MS 50

#define SEEK_UP 1
#define SEEK_DOWN 0

#define SPACING_200kHz 0
#define SPACING_100kHz 1
#define SPACING_50kHz 2

#define BAND_USA_EUROPE 0
#define BAND_JAPAN_WIDE 1
#define BAND_JAPAN_STANDARD 2

#define MAX_RADIO_FREQ_MHZ 108

typedef enum
{
    rEurope,
    rUSA,
    rAustralia,
    rJapan,

} radio_region_t;

typedef struct
{
    char nazwaStacji[30];
    char textStacji[64];
    uint8_t godzina;
    uint8_t minuta;

} rds_data_t;

typedef enum
{
    notInitialized,
    seekInit,
    loopUntilSTC,
    stationFound,
    seekDeinit,
    callback,

} seek_state_t;

typedef struct
{
    seek_state_t seek_state;
    uint8_t seek_dir;

} seek_control_t;

typedef void (*user_callback_t)(void);

class Si4703
{
  public:
    Si4703() { instance = this; }

    void init(int resetPin, int sdioPin, int sclkPin, int interruptPin);
    void powerOn(); // call in setup
    void powerOff();
    void seekUp(); // returns the tuned channel or 0
    void seekDown();
    void setVolume(int volume); // 0 to 15

    bool setFrequency(float freq);
    bool update_RDS();
    float getFrequency();
    int getRSSI();

    void stateMachineSeek();

    void setSeekCallback(user_callback_t callback);
    void resetSeekCallback();

    void clearRDS();

    uint8_t getInitializationStatus();

    uint8_t rdsReadyFlag;

    RDSParser rds;
    rds_data_t rds_data;

    seek_control_t seek_control;

    void setRegion(radio_region_t region, uint8_t init, bool ignoreErrors = false);
    float freqSpacing_MHz;
    float freqBottomBase_MHz;

    uint8_t regionHasChanged;

    radio_region_t currentRegion;

    float getRadioSpacing();
    float getRadioBottomBase();
    uint8_t hasRegionChanged();

    static Si4703* instance;

  private:
    int _resetPin;
    int _sdioPin;
    int _sclkPin;
    int _interruptPin;

    uint8_t readRegisters(bool ignoreErrors = false);
    uint8_t updateRegisters(bool ignoreErrors = false);
    float seek(byte seekDirection);
    void initSeek(uint8_t seekDirection);

    uint8_t isInitializedProperly = 0;
    uint16_t si4703_registers[16]; // There are 16 registers, each 16 bits large
};

#endif
