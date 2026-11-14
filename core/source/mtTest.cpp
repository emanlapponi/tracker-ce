#include "Audio.h"
#include "FT812.h"
#include "Recorder.h"
#include "core/modulesBase.h"
#include "mtHardware.h"
#include "mtMidi.h"
#include "mtPadsBacklight.h"
#include "mtSleep.h"

#include "mtTest.h"

#include "Platform.h"

cTest mtTest;
static cTest* TP = &mtTest;

extern int16_t* externalRamEffectsBuff;
extern int16_t externalRamDelayBuff[];
extern uint16_t externalRamReverbBuff[];

static void functButtons(uint8_t button, uint8_t state);
static void functEncoder(int16_t value);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functPowerButton(uint8_t state);

static void receiveTestNoteOn(byte channel, byte pitch, byte velocity);
static void radioSeek();

const char checkList[checkCount][20] = {
    "Start", "Screen", "Inputs", "USB/SD", "RAM", "MIDI", "Audio", "Radio", "Battery", "Finished",
};

//==========================================================================================
//

void cTest::setRadio(Radio* radio)
{
    cTest::radio = radio;
}

void cTest::setCodecAudio(Sgtl5000* codec)
{
    cTest::codec = codec;
}

void cTest::setButtons(SwitchGroup* buttons)
{
    cTest::buttons = buttons;
}

void cTest::setEncoder(Encoder* encoder)
{
    cTest::encoder = encoder;
}

void cTest::setTimer(Timer* timer)
{
    cTest::timer = timer;
}

void cTest::setBattery(Battery* battery)
{
    cTest::battery = battery;
}

//==========================================================================================
void cTest::runTestingProcedure(InputProcessor* ip)
{
    cTest::ip = ip;

    if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP)
    {
        sequencer.stop();
    }

    radio->enable(true);
    radio->setRegion((Radio::Region)mtConfig.general.radioRegion);
    radio->setFrequency(93.7f);
    radio->setSeekCallback(radioSeek);
    buttons->setHoldTime(1200);
    display.disable();
    display.clear();
    encoder->setAcceleration(0);
    memset(&results, 0, checkCount);

    for (uint8_t button = 0; button < interfaceButtonsCount; button++)
    {
        ip->setButtonObj(button, functButtons);
    }

    ip->setPotObj(interfacePot0, functEncoder);
    ip->setPadsGlobal(functPads);
    ip->setPowerButtonObj(functPowerButton);

    mainStatus = checkStart;
    // mainStatus = checkAudio;//checkInputs;
    procedureRunning = 1;
}

//==========================================================================================
//
//==========================================================================================
void cTest::testLoop()
{
    if (!procedureRunning)
        return;

    if (guiRefresh < 100)
        return;
    guiRefresh = 0;

    // Graficzne
    drawGui();

    // Funkcyjne
    doTasks();
}

//==========================================================================================
//
//==========================================================================================
void cTest::drawGui()
{
    display.beginScreen();

    API_VERTEX_FORMAT(0);
    API_CLEAR_COLOR(0x000000);
    API_CLEAR(1, 1, 1);

    if (!hideStatus)
        showStatus();

    switch (mainStatus)
    {
        case checkStart: showStart(); break;
        case checkScreen: showScreenTest(); break;
        case checkInputs: showInputsTest(); break;
        case checkUSB: showUSBTest(); break;
        case checkRAM: showRAMTest(); break;
        case checkMidi: showMidiTest(); break;
        case checkRadio: showRadioTest(); break;
        case checkBattery: showBatteryTest(); break;
        case checkAudio: showAudioTest(); break;

        case checkEnd: showEnd(); break;
    }

    display.showScreen();
}

void cTest::doTasks()
{
    switch (mainStatus)
    {
        case checkStart: break;

        case checkScreen: runScreenTest(); break;
        case checkInputs: runInputsTest(); break;
        case checkUSB: break;
        case checkRAM: runRAMTest(); break;
        case checkMidi: runMidiTest(); break;
        case checkRadio: runRadioTest(); break;
        case checkBattery: runBatteryTest(); break;
        case checkAudio: runAudioTest(); break;

        case checkEnd: break;
    }
}

//==========================================================================================
//
//==========================================================================================
void cTest::showStart()
{
    showMessage("Pres Run to start the test", "", "Run", "");
}

void cTest::showEnd()
{
#ifndef TRACKER_MINI
    showMessage(
      "Test Done", "Sending data to Polyend headquarters...", "Restart device", "Back to Start");
#else
    showMessage("To finish, test the reset function.",
                "Hold the power button for 5 seconds.",
                "",
                "Back to Start");
#endif
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showScreenTest()
{
    if (testPhase == 0)
    {
        showMessage("Screen test", "", "Start", "Skip");
    }
    if (testPhase == 2)
    {
        API_CLEAR_COLOR(0x000000);
        API_CLEAR(1, 1, 1);

        API_CMD_GRADIENT(0, 0, 0x0, 800, 0, 0xff0000);

        // API_CMD_GRADIENT(0, 0, 0xffffff, 800, 0, 0x00ffff);
    }
    else if (testPhase == 5)
    {
        API_CLEAR_COLOR(0x000000);
        API_CLEAR(1, 1, 1);

        API_CMD_GRADIENT(0, 0, 0x0, 800, 0, 0x00ff00);
        // API_CMD_GRADIENT(0, 0, 0xffffff, 800, 0, 0xff00ff);
    }
    else if (testPhase == 8)
    {
        API_CLEAR_COLOR(0x000000);
        API_CLEAR(1, 1, 1);

        API_CMD_GRADIENT(0, 0, 0x0, 800, 0, 0x0000ff);

        // API_CMD_GRADIENT(0, 0, 0xffffff, 800, 0, 0xffff00);
    }
    else if (testPhase == 11)
    {
        API_CLEAR_COLOR(0x000000);
        API_CLEAR(1, 1, 1);

        API_CMD_GRADIENT(0, 0, 0x0, 800, 0, 0xffffff);
    }
    else if ((testPhase - 1) % 3 == 2)
    {
        showMessage("Is screen OK?", "", "Yes", "No");
    }
}

void cTest::runScreenTest()
{
    if (testPhase == 0)
    {
        return;
    }
    else if (testPhase == 12)
    {
        testTimer = 0;
        hideStatus = 0;
    }
    else if (testPhase > 0 && (testPhase - 1) % 3 == 0)
    {
        testTimer = 0;
        hideStatus = 1;
        testPhase++;
    }
    else if (testPhase > 0 && (testPhase - 1) % 3 == 1 && testTimer > 2000)
    {
        testTimer = 0;
        hideStatus = 1;
        testPhase++;
    }
    else if (testPhase > 0 && (testPhase - 1) % 3 == 2)
    {
        testTimer = 0;
        hideStatus = 0;
    }
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showInputsTest()
{
#ifndef TRACKER_MINI

    uint16_t baseX = 420, baseY = 320;

    API_BEGIN(RECTS);

    ledBright++;
    if (ledBright > 31)
        ledBright = 0;

    // pady
    for (uint8_t i = 0; i < 48; i++)
    {
        if (inputs.pads[i] == 0)
            API_COLOR(0xff0000);
        else if (inputs.pads[i] == 1)
            API_COLOR(0xffff00);
        else if (inputs.pads[i] == 2)
        {
            API_COLOR(0x00ff00);
            padsBacklight.setFrontLayer(1, ledBright, i);
        }
        API_VERTEX2F(baseX + ((i % 12) * 18), baseY + (i / 12) * 18);
        API_VERTEX2F(baseX + ((i % 12) * 18) + 13, baseY + ((i / 12) * 18) + 13);
    }

    // buttony
    for (uint8_t i = 0; i < 33; i++)
    {
        if (inputs.buttons[i] == 0)
            API_COLOR(0xff0000);
        else if (inputs.buttons[i] == 1)
            API_COLOR(0xffff00);
        else if (inputs.buttons[i] == 2)
            API_COLOR(0x00ff00);

        if (i < 8)
        {
            API_VERTEX2F(baseX + ((i)*27), baseY - 40);
            API_VERTEX2F(baseX + ((i)*27) + 21, baseY - 19);
        }
        else if (i < 23)
        {
            uint8_t xy = i - 8;
            API_VERTEX2F(baseX + 230 + ((xy % 5) * 27), baseY - 175 + (xy / 5) * 27);
            API_VERTEX2F(baseX + 230 + ((xy % 5) * 27) + 21, baseY - 175 + ((xy / 5) * 27) + 21);
        }
        else
        {
            uint8_t xy = i - 23;
            API_VERTEX2F(baseX + 230 + ((xy % 5) * 27), baseY - 75 + (xy / 5) * 27);
            API_VERTEX2F(baseX + 230 + ((xy % 5) * 27) + 21, baseY - 75 + ((xy / 5) * 27) + 21);
        }
    }

    // power button
    if (inputs.powerButton == 0)
        API_COLOR(0xff0000);
    else if (inputs.powerButton == 1)
        API_COLOR(0xffff00);
    else if (inputs.powerButton == 2)
        API_COLOR(0x00ff00);
    API_VERTEX2F(baseX, baseY - 195);
    API_VERTEX2F(baseX + 15, baseY - 195 + 15);
    API_END();

    // encoder
    API_BEGIN(FTPOINTS);
    API_POINT_SIZE(40 * 16);

    uint32_t enc_color = 0xff0000;
    if (inputs.encoderL == 0 && inputs.encoderR == 1)
        enc_color = 0xffff00;
    else if (inputs.encoderR == 0 && inputs.encoderL == 1)
        enc_color = 0xffff00;
    else if (inputs.encoderR == 1 && inputs.encoderL == 1)
        enc_color = 0x00ff00;
    else
        enc_color = 0xff0000;

    API_COLOR(enc_color);
    API_VERTEX2F(baseX + 69 + 230, baseY + 36);
    API_END();

    // suwak enkodera
    API_BEGIN(LINE_STRIP);
    API_COLOR(0x0000ff);
    API_VERTEX2F(baseX, baseY + 90);
    API_VERTEX2F(baseX + 360, baseY + 90);
    API_VERTEX2F(baseX + 360, baseY + 120);
    API_VERTEX2F(baseX, baseY + 120);
    API_VERTEX2F(baseX, baseY + 90);
    API_END();

    uint16_t encoder_temp = inputs.encoderValue + 175;

    API_BEGIN(RECTS);
    API_COLOR(enc_color);
    API_VERTEX2F(baseX + ((350 * encoder_temp) / 350), baseY + 90);
    API_VERTEX2F(baseX + 10 + ((350 * encoder_temp) / 350), baseY + 120);

    API_END();

    if (testPhase == 0)
    {
        showMessage("Buttons, pads and encoder test", "", "Start", "Skip");
    }
    if (testPhase == 2)
    {
        showMessage("Press all the buttons, pads and move", "the encoder", "", "Hold to skip");
    }
    if (testPhase == 3)
    {
        showMessage("Is pads backlight ok?", "", "Yes", "Skip");
    }

#else
    // MINI
    uint16_t baseX = 420, baseY = 200;

    API_BEGIN(RECTS);
    // power button
    if (inputs.powerButton == 0)
        API_COLOR(0xff0000);
    else if (inputs.powerButton == 1)
        API_COLOR(0xffff00);
    else if (inputs.powerButton == 2)
        API_COLOR(0x00ff00);
    API_VERTEX2F(baseX + 300, baseY - 150);
    API_VERTEX2F(baseX + 315, baseY - 150 + 15);

    // buttony
    for (uint8_t i = 0; i < interfaceButtonsCount; i++)
    {
        if (inputs.buttons[i] == 0)
            API_COLOR(0xff0000);
        else if (inputs.buttons[i] == 1)
            API_COLOR(0xffff00);
        else if (inputs.buttons[i] == 2)
            API_COLOR(0x00ff00);
        else if (inputs.buttons[i] == 3)
            API_COLOR(0x0000ff);

        if (i < 8)
        {
            API_VERTEX2F(baseX + ((i)*40), baseY);
            API_VERTEX2F(baseX + ((i)*40) + 20, baseY + 20);
        }
        else if (i < 18)
        {
        }
        else if (i < 22)
        {
            API_VERTEX2F(baseX + 80 + ((i - 18) * 40), baseY + 40);
            API_VERTEX2F(baseX + 80 + ((i - 18) * 40) + 20, baseY + 40 + 20);
        }
        else if (i >= 35 && i <= 38)
        {
            API_VERTEX2F(baseX + 80 + ((i - 35) * 40), baseY + 200);
            API_VERTEX2F(baseX + 80 + ((i - 35) * 40) + 20, baseY + 200 + 20);
        }
        else
        {
            switch (i)
            {
                case 23:
                    API_VERTEX2F(baseX + 120, baseY + 80);
                    API_VERTEX2F(baseX + 120 + 20, baseY + 80 + 20);
                    break;
                case 28:
                    API_VERTEX2F(baseX + 160, baseY + 80);
                    API_VERTEX2F(baseX + 160 + 20, baseY + 80 + 20);
                    break;
                case interfaceButtonInsert:
                    API_VERTEX2F(baseX + 120, baseY + 120);
                    API_VERTEX2F(baseX + 120 + 20, baseY + 120 + 20);
                    break;
                case interfaceButtonMiniMenu:
                    API_VERTEX2F(baseX + 160, baseY + 120);
                    API_VERTEX2F(baseX + 160 + 20, baseY + 120 + 20);
                    break;
                case interfaceButtonDelete:
                    API_VERTEX2F(baseX + 120, baseY + 160);
                    API_VERTEX2F(baseX + 120 + 20, baseY + 160 + 20);
                    break;
                case interfaceButtonCopy:
                    API_VERTEX2F(baseX + 160, baseY + 160);
                    API_VERTEX2F(baseX + 160 + 20, baseY + 160 + 20);
                    break;
                case 29:
                    API_VERTEX2F(baseX + 120, baseY + 240);
                    API_VERTEX2F(baseX + 120 + 20, baseY + 240 + 20);
                    break;
                case 43:
                    API_VERTEX2F(baseX + 160, baseY + 240);
                    API_VERTEX2F(baseX + 160 + 20, baseY + 240 + 20);
                    break;

                case 26:
                    API_VERTEX2F(baseX + 40, baseY + 80);
                    API_VERTEX2F(baseX + 40 + 20, baseY + 80 + 20);
                    break;
                case 30:
                    API_VERTEX2F(baseX + 0, baseY + 120);
                    API_VERTEX2F(baseX + 0 + 20, baseY + 120 + 20);
                    break;
                case 33:
                    API_VERTEX2F(baseX + 40, baseY + 120);
                    API_VERTEX2F(baseX + 40 + 20, baseY + 120 + 20);
                    break;
                case 32:
                    API_VERTEX2F(baseX + 80, baseY + 120);
                    API_VERTEX2F(baseX + 80 + 20, baseY + 120 + 20);
                    break;
                case 31:
                    API_VERTEX2F(baseX + 40, baseY + 160);
                    API_VERTEX2F(baseX + 40 + 20, baseY + 160 + 20);
                    break;

                case 39:
                    API_VERTEX2F(baseX + 240, baseY + 80);
                    API_VERTEX2F(baseX + 240 + 20, baseY + 80 + 20);
                    break;
                case 40:
                    API_VERTEX2F(baseX + 240, baseY + 120);
                    API_VERTEX2F(baseX + 240 + 20, baseY + 120 + 20);
                    break;
                case 41:
                    API_VERTEX2F(baseX + 240, baseY + 160);
                    API_VERTEX2F(baseX + 240 + 20, baseY + 160 + 20);
                    break;
                case 42:
                    API_VERTEX2F(baseX + 240, baseY + 200);
                    API_VERTEX2F(baseX + 240 + 20, baseY + 200 + 20);
                    break;
            }
        }
    }
    API_END();

    if (testPhase == 0)
    {
        showMessage("Buttons test", "", "Start", "Skip");
    }
    else if (testPhase == 2)
    {
        showMessage("Press all the buttons", "", "", "Hold to skip");
    }
    else if (testPhase == 4)
    {
        showMessage("All buttons have been pressed", "", "Continue", "");
    }
#endif
}

void cTest::runInputsTest()
{
    if (testPhase == 0)
    {
    }
    else if (testPhase == 1)
    {
        memset(&inputs, 0, sizeof(inputs));
        testPhase++;
    }
    else if (testPhase == 2)
    {
        uint8_t input_result = 1;

#ifndef TRACKER_MINI

        for (uint8_t i = 0; i < 48; i++)
        {
            if (inputs.pads[i] < 2)
                input_result = 0;
        }
        for (uint8_t i = 0; i < 33; i++)
        {
            if (inputs.buttons[i] < 2)
                input_result = 0;
        }
        if (inputs.powerButton < 2)
            input_result = 0;
        if (inputs.encoderL < 1)
            input_result = 0;
        if (inputs.encoderR < 1)
            input_result = 0;

#else
        for (uint8_t i = 0; i < 8; i++)
        {
            if (inputs.buttons[i] < 2)
                input_result = 0;
        }
        for (uint8_t i = 18; i <= 21; i++)
        {
            if (inputs.buttons[i] < 2)
                input_result = 0;
        }
        for (uint8_t i = 23; i <= 43; i++)
        {
            if (inputs.buttons[i] < 2)
                input_result = 0;
        }

        if (inputs.powerButton < 2)
        {
            input_result = 0;
        }
#endif

        if (input_result == 1)
        {
            testPhase++;
        }
    }
    else if (testPhase == 3)
    {
#ifdef TRACKER_MINI
        testPhase++;
#endif
    }
    else if (testPhase == 4)
    {
    }
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showRAMTest()
{
    if (testPhase == 0)
    {
        showMessage("RAM memory test", "", "Start", "Skip");
    }
    else if (testPhase == 1)
    {
        showMessage("Testing RAM...", "", "", "");
    }
    else if (testPhase == 2)
    {
        char errors[50];
        sprintf(errors,
                "passed: %u/10, max errors: %u",
                (unsigned int)ramTestsPassed,
                (unsigned int)ramErrorsMax);
        showMessage("RAM test failed", errors, "Ok", "Retry");
    }
    else if (testPhase == 3)
    {
        char errors[50];
        sprintf(errors,
                "passed: %u/10, max errors: %u",
                (unsigned int)ramTestsPassed,
                (unsigned int)ramErrorsMax);
        showMessage("RAM test ended successfully", errors, "Ok", "Retry");
    }
}

void cTest::runRAMTest()
{
    if (testPhase == 0)
    {
    }
    else if (testPhase == 1)
    {
        AudioNoInterrupts();

        ramErrorsMax = 0;
        ramTestsPassed = 0;
        uint32_t tests_result[10];

        for (uint8_t test = 0; test < 10; test++)
        {
            uint32_t ramErrorsCounter = 0;
            uint32_t* ptrMem = (uint32_t*)EXTERAL_MEMORY_ADDRESS;

            for (uint32_t i = 0; i < TOTAL_EXTERNAL_MEMORY / 4; i++)
            {
                *(ptrMem + i) = i;
            }

            timer->delay(10);

            for (uint32_t i = 0; i < TOTAL_EXTERNAL_MEMORY / 4; i++)
            {
                uint32_t read = *(ptrMem + i);
                if (read != i)
                {
                    ramErrorsCounter++;
                }
            }

            tests_result[test] = ramErrorsCounter;
            if (ramErrorsCounter == 0)
                ramTestsPassed++;
            if (ramErrorsCounter > ramErrorsMax)
                ramErrorsMax = ramErrorsCounter;
        }

        memset(externalRamDelayBuff, 0, DELAY_MEMORY_SIZE);
        memset(externalRamReverbBuff, 0, REVERB_MEMORY_SIZE);

        AudioInterrupts();

        (void)tests_result;

        if (ramTestsPassed == 10)
        {
            testPhase = 3;
        }
        else
        {
            testPhase = 2;
        }
    }
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showMidiTest()
{
    if (testPhase == 0)
    {
        showMessage("Connect MIDI cable", "IN <-> OUT", "Ready", "Skip");
    }
    else if (testPhase == 1 || testPhase == 2)
    {
        showMessage("Testing MIDI", "", "", "");
    }
    else if (testPhase == 3)
    {
        showMessage("Test failed", "Is cable connected properly?", "Retry", "Skip");
    }
    else if (testPhase == 4)
    {
        showMessage("MIDI test ended successfully", "", "OK", "Retry");
    }
}

void cTest::runMidiTest()
{
    if (testPhase == 0)
    {
        midiCounter = 0;
        midiResults = 0;

        setHandleNoteOnAction(receiveTestNoteOn);
    }
    else if (testPhase == 1)
    {
        testTimer = 0;
        resetMidi();

        for (uint8_t i = 0; i < 8; i++)
        {
            sendNoteOnRaw(midiNotes[i], midiVelos[i], midiChannels[i]);
        }

        testPhase = 2;
    }
    else if (testPhase == 2)
    {
        if (midiResults == 255)
            testPhase = 4;

        if (testTimer > 3000)
            testPhase = 3;
    }
    else if (testPhase == 3)
    {
    }
    else if (testPhase == 4)
    {
    }
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showRadioTest()
{
    if (testPhase == 0)
    {
        showMessage("Connect Headphones", "", "Ready", "Skip");
    }
    else if (testPhase == 1 || testPhase == 2)
    {
        showMessage((char*)"Is radio Quailty ok?", radioFreq, (char*)"Yes", (char*)"Skip");
    }
}

void cTest::runRadioTest()
{
    if (testPhase == 0)
    {
    }
    else if (testPhase == 1)
    {
        radioSeek();
        setAudiIO(2);
        testPhase = 2;
        testTimer = 0;
    }
    else if (testPhase == 2)
    {
        if (testTimer > 3000)
        {
            radio->seek(Radio::SeekDirection::kUp);
            testTimer = 0;
        }
        radio->updateSeek();
    }
    else if (testPhase == 3)
    {
        setAudiIO(0);
        nextTest();
    }
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showBatteryTest()
{
    if (testPhase == 0)
    {
        char batteryDataText[50] = { 0 };

        if (battery->getState())
        {
            sprintf(batteryDataText,
                    "%d%% %.2fV %s",
                    battery->getLevel(),
                    battery->getVoltage(),
                    battery->getCharging() ? "charging " : "no charging");
        }

        showMessage("Is battery data correct?", batteryDataText, "Ok", "Skip");
    }
}

void cTest::runBatteryTest()
{
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showAudioTest()
{
    if (testPhase == 0)
    {
        showMessage("Connect audio output to line input", "", "Ready", "Skip");
    }
    else if (testPhase == 1 || testPhase == 2)
    {
        showMessage("Recording in progress", "Wait...", "", "");
    }
    else if (testPhase == 3)
    {
#ifdef TRACKER_MINI
        showMessage("Make any sound into the microphone", "", "Ready", "");
#else
        showMessage("Connect audio output to mic input", "", "Ready", "");
#endif
    }
    else if (testPhase == 4 || testPhase == 5)
    {
        showMessage("Recording in progress", "Wait...", "", "");
    }
    else if (testPhase == 6)
    {
        showMessage("Connect Headphones", "", "Ready", "");
    }
    else if (testPhase == 7 || testPhase == 8)
    {
        showMessage("Listen and check the quality of recording", "-line in", "All good", "Skip");
    }
    else if (testPhase == 9 || testPhase == 10)
    {
        showMessage("Listen and check the quality of recording", "-mic", "All good", "Skip");
    }
    else if (testPhase == 11 || testPhase == 12)
    {
        showMessage("Audio test complited", "", "Ok", "Retry");
    }
}

void cTest::runAudioTest()
{
    if (testPhase == 0)
    {
    }
    else if (testPhase == 1)
    {
        // sequencer.stop();
        memset(externalRamEffectsBuff, 0, EDIT_BUFF_SIZE);

        engine.disableLineInImpact();
        setAudiIO(0);
        timer->delay(1);

        if (fileSystem->exists(kLineInTestFile))
        {
            playSdFile(kLineInTestFile);
        }
        else
        {
            engine.startTestSignal(0.1, 100);
        }

        testTimer = 0;
        Recorder::Config config;
        config.destinationBufferAddress = externalRamEffectsBuff;
        config.destinationBufferSize = EDIT_BUFF_SIZE;
        config.destinationChannels = 1;
        config.sourceChannels = 2;

        recorder.setConfig(config);
        recorder.record();
        testPhase = 2;
    }
    else if (testPhase == 2 || testPhase == 5)
    {
        if (testTimer > 2000)
        {
            recorder.stop();
            stopPlayingSdFile();
            engine.stopTestSignal();
            testPhase++;
        }
    }
    else if (testPhase == 4)
    {
        setAudiIO(1);
        timer->delay(1);

        if (fileSystem->exists(kMicTestFile))
        {
            playSdFile(kMicTestFile);
        }
        else
        {
            engine.startTestSignal(0.1, 1000);
        }

        testTimer = 0;

        Recorder::Config config;
        config.destinationBufferAddress = externalRamEffectsBuff + 200000;
        config.destinationBufferSize = EDIT_BUFF_SIZE;
        config.destinationChannels = 1;
        config.sourceChannels = 1;

        recorder.setConfig(config);
        recorder.record();

        testPhase = 5;
    }
    else if (testPhase == 7)
    {
        setAudiIO(0);
        instrumentPlayer[0].noteOff();

        std::vector<int16_t*> addresses;
#ifdef PLATFORM_IMX
        addresses.push_back(externalRamEffectsBuff);
        addresses.push_back(externalRamEffectsBuff);
#else

        addresses.push_back(externalRamEffectsBuff);
#endif

        instrumentPlayer[0].noteOnWithoutInstrument(addresses, 70000, mtSampleTypeWaveFile);
        testTimer = 0;
        testPhase = 8;
    }
    else if (testPhase == 8)
    {
        if (testTimer > 1600)
        {
            std::vector<int16_t*> addresses;
#ifdef PLATFORM_IMX
            addresses.push_back(externalRamEffectsBuff);
            addresses.push_back(externalRamEffectsBuff);
#else

            addresses.push_back(externalRamEffectsBuff);
#endif
            instrumentPlayer[0].noteOff();
            instrumentPlayer[0].noteOnWithoutInstrument(addresses, 70000, mtSampleTypeWaveFile);
            testTimer = 0;
        }
    }
    else if (testPhase == 9)
    {
        std::vector<int16_t*> addresses;
#ifdef PLATFORM_IMX
        addresses.push_back(externalRamEffectsBuff + 200000);
        addresses.push_back(externalRamEffectsBuff + 200000);
#else

        addresses.push_back(externalRamEffectsBuff + 200000);
#endif
        setAudiIO(0);
        instrumentPlayer[0].noteOff();
        instrumentPlayer[0].noteOnWithoutInstrument(addresses, 70000, mtSampleTypeWaveFile);
        testTimer = 0;
        testPhase = 10;
    }
    else if (testPhase == 10)
    {
        if (testTimer > 1600)
        {
            instrumentPlayer[0].noteOff();
            std::vector<int16_t*> addresses;
#ifdef PLATFORM_IMX
            addresses.push_back(externalRamEffectsBuff + 200000);
            addresses.push_back(externalRamEffectsBuff + 200000);
#else

            addresses.push_back(externalRamEffectsBuff + 200000);
#endif

            instrumentPlayer[0].noteOnWithoutInstrument(addresses, 70000, mtSampleTypeWaveFile);
            testTimer = 0;
        }
    }
    else if (testPhase == 11)
    {
        instrumentPlayer[0].noteOff();
        testPhase = 12;
    }
    else if (testPhase == 12)
    {
    }
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showUSBTest()
{
    showMessage("Is USB and SD card working properly?",
                "(both were used for initial programming)",
                "Yes",
                "No/Skip");
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showStatus()
{

    API_CLEAR_COLOR(0);
    API_CLEAR(1, 1, 1);

    API_COLOR(0xFFFFFF);

    API_CMD_TEXT(30, 30, 28, 0, "Polyend Tracker testing procedure");

    int activeStep = 0;

    for (uint8_t step = 0; step < checkCount; step++)
    {
#ifdef TRACKER_MINI
        if (step == checkRadio)
            continue;
#else
        if (step == checkBattery)
            continue;
#endif
        if (step > mainStatus)
            API_COLOR(0xFF0000);
        else if (step == mainStatus)
            API_COLOR(0xFFFF00);
        else if (results[step] == 1)
            API_COLOR(0xFF0000);
        else
            API_COLOR(0x00FF00);

        API_CMD_TEXT(50, 80 + activeStep * 25, 28, 0, &checkList[step][0]);

        activeStep++;
    }

    API_COLOR(0xFFFFFF);
    API_BEGIN(LINES);

    API_VERTEX2F(0, 330);
    API_VERTEX2F(400, 330);

    API_VERTEX2F(400, 0);
    API_VERTEX2F(400, 479);

    API_END();
}

//==========================================================================================
//==========================================================================================
//==========================================================================================
void cTest::showMessage(const char* question1,
                        const char* question2,
                        const char* answer1,
                        const char* answer2)
{
    API_COLOR(0xFFFFFF);
    API_CMD_TEXT(5, 350, 28, 0, question1);
    API_CMD_TEXT(5, 380, 28, 0, question2);

    API_CMD_TEXT(5, 450, 28, 0, answer1);
    API_CMD_TEXT(400 - 5, 450, 28, OPT_RIGHTX, answer2);
}

void cTest::nextTest()
{
    padsBacklight.clearGlobalPads(1, 1, 1);
    if (mainStatus < checkEnd)
    {
        mainStatus++;
    }

#ifdef TRACKER_MINI
    if (mainStatus == checkRadio)
    {
        nextTest();
    }
#else
    if (mainStatus == checkBattery)
    {
        nextTest();
    }
#endif

    testPhase = 0;
    hideStatus = 0;
}

//==========================================================================================
//
//==========================================================================================
void cTest::AcceptButton()
{
    switch (mainStatus)
    {
        case checkStart:
        {
            mainStatus++;
            break;
        }
        case checkScreen:
        {
            if (testPhase < 12 && (testPhase == 0 || (testPhase - 1) % 3 == 2))
            {
                testPhase++;
            }
            else if (testPhase == 12)
            {
                nextTest();
            }
            break;
        }
        case checkInputs:
        {
            if (testPhase == 0)
            {
                testPhase = 1;
            }
            else if (testPhase >= 3)
            {
                nextTest();
            }
            break;
        }
        case checkUSB:
        {
            nextTest();
            break;
        }
        case checkRAM:
        {
            if (testPhase == 0)
            {
                testPhase = 1;
            }
            else if (testPhase == 2)
            {
                results[checkRAM] = 1;
            }
            else if (testPhase > 2)
            {
                nextTest();
            }
            break;
        }
        case checkMidi:
        {
            if (testPhase == 1)
                break;
            if (testPhase < 3)
                testPhase++;
            else if (testPhase == 3)
                testPhase = 0; // nextTest();
            else if (testPhase == 4)
                nextTest();
            break;
        }
        case checkRadio:
        {
            if (testPhase < 4)
            {
                testPhase++;
            }
            else
            {
                nextTest();
            }
            break;
        }
        case checkBattery:
        {
            nextTest();
            break;
        }
        case checkAudio:
        {
            if (testPhase == 0 || testPhase == 3 || testPhase == 6 || testPhase == 7 ||
                testPhase == 8 || testPhase == 9 || testPhase == 10)
            {
                testPhase++;
            }
            else if (testPhase == 11 || testPhase == 12)
            {
                nextTest();
            }
            break;
        }
        case checkEnd:
        {
#ifndef PLATFORM_IMX
            systemReset();
#endif
            break;
        }
    }
}

void cTest::DeclineButton()
{
    switch (mainStatus)
    {
        case checkStart:
        {
            mainStatus++;
            break;
        }
        case checkScreen:
        {
            results[checkScreen] = 1;
            if (testPhase > 0 && testPhase < 12)
            {
                testPhase++;
            }
            else
            {
                nextTest();
            }
            break;
        }
        case checkInputs:
        {
            if (testPhase == 0 || testPhase >= 3)
            {
                results[checkInputs] = 1;
                nextTest();
            }
            break;
        }
        case checkUSB:
        {
            results[checkUSB] = 1;
            nextTest();
            break;
        }
        case checkRAM:
        {
            if (testPhase == 0)
            {
                results[checkRAM] = 1;
                nextTest();
            }
            else if (testPhase == 2 || testPhase == 3)
            {
                testPhase = 1;
            }
            break;
        }
        case checkMidi:
        {
            if (testPhase == 0 || testPhase == 3)
            {
                results[checkMidi] = 1;
                nextTest();
            }
            else if (testPhase == 4)
            {
                testPhase = 0;
            }
            break;
        }
        case checkRadio:
        {
            if (testPhase == 0)
            {
                results[checkRadio] = 1;
                nextTest();
            }
            break;
        }
        case checkBattery:
        {
            if (testPhase == 0)
            {
                results[checkBattery] = 1;
                nextTest();
            }
            break;
        }
        case checkAudio:
        {
            if (testPhase == 0)
            {
                results[checkAudio] = 1;
                nextTest();
            }
            else if (testPhase == 7 || testPhase == 8 || testPhase == 9 || testPhase == 10)
            {
                testPhase++;
            }
            else if (testPhase == 11 || testPhase == 12)
            {
                testPhase = 0;
            }
            break;
        }
        case checkEnd:
        {
            mainStatus = checkStart;
            memset(&results, 0, checkCount);
            testPhase = 0;
            break;
        }
    }
}

void cTest::skipButton()
{
    if (blockSkip == 1)
    {
        return;
    }
    results[mainStatus] = 1;
    nextTest();
    blockSkip = 1;
}
//==========================================================================================
//
//==========================================================================================
uint8_t cTest::runTestByCombinaion(uint8_t pad)
{
    if (startCode[pos++] != pad)
    {
        pos = 0;
    }
    else if (pos == 4) // == 5
    {
        pos = 0;
        return 1;
    }
    return 0;
}

uint8_t cTest::runTestByMiniCombinaion(uint8_t pad)
{
    if (startCodeMini[pos++] != pad)
    {
        pos = 0;
    }
    else if (pos == 4) // == 5
    {
        pos = 0;
        return 1;
    }
    return 0;
}

void cTest::systemReset()
{
    lowPower.resetMCU();
}

static void receiveTestNoteOn(byte channel, byte pitch, byte velocity)
{
    if (TP->midiNotes[TP->midiCounter] == pitch && TP->midiVelos[TP->midiCounter] == velocity &&
        TP->midiChannels[TP->midiCounter] == channel)
    {
        TP->midiResults |= (1 << TP->midiCounter);
    }

    TP->midiCounter++;

    if (TP->midiCounter >= 8)
    {
        TP->midiCounter = 0;
    }
}

static void radioSeek()
{
    sprintf(TP->radioFreq, "%.2f MHz", TP->getRadio()->getFrequency());
}

void cTest::setAudiIO(uint8_t config)
{
    if (config == 0)
    {
        codec->setInputType(Sgtl5000::InputType::kLineIn);
        radio->enableLineInput(false);
        codec->setHeadphoneSource(Sgtl5000::HeadphoneSource::kAudioOutput);
        codec->setLineInLevel(15);
        engine.setHeadphonesVolume(70);
    }
    else if (config == 1)
    {
        codec->setInputType(Sgtl5000::InputType::kMic);
        radio->enableLineInput(false);
        codec->setHeadphoneSource(Sgtl5000::HeadphoneSource::kAudioOutput);
        codec->setMicGain(20);
        engine.setHeadphonesVolume(70);
    }
    else if (config == 2)
    {
        codec->setInputType(Sgtl5000::InputType::kLineIn);
        radio->enableLineInput(true);
        codec->setHeadphoneSource(Sgtl5000::HeadphoneSource::kAudioInput);
        codec->setLineInLevel(15);
        engine.setHeadphonesVolume(70);
    }
}

void cTest::playSdFile(const char* filePath)
{
    stopPlayingSdFile();

    if (!fileSystem->exists(filePath))
    {
        return;
    }

    engine.getSdPlayer()->play(filePath);
}

void cTest::stopPlayingSdFile()
{
    engine.getSdPlayer()->stop();
}

//==========================================================================================
//
//==========================================================================================
static void functButtons(uint8_t button, uint8_t state)
{

#ifdef TRACKER_MINI
    if (TP->getMainStatus() == checkInputs && TP->getTestPhase() > 0)
    {
        if (button == interfaceButtonShift)
        {
            if (TP->inputs.buttons[interfaceButtonShift] == 2 && state == buttonPress &&
                TP->inputs.buttons[43] == 0)
            {
                TP->inputs.buttons[43] = 2;
            }
            else if (TP->inputs.buttons[interfaceButtonShift] == 2 && state == buttonRelease &&
                     TP->inputs.buttons[43] == 1)
            {
                TP->inputs.buttons[43] = 2;
            }
        }

        if (state == buttonPress || state == buttonRelease)
        {
            TP->inputs.buttons[button]++;
            if (TP->inputs.buttons[button] > 3)
            {
                TP->inputs.buttons[button] = 1;
            }
        }
    }

#else
    if (TP->getMainStatus() == checkInputs && TP->getTestPhase() > 0)
    {
        if (button == interfaceButtonShift && TP->inputs.buttons[interfaceButtonShift] == 2 &&
            state == buttonPress && TP->inputs.buttons[43] == 0)
        {
            TP->inputs.buttons[43] = 2;
        }
        else if (button == interfaceButtonShift && TP->inputs.buttons[interfaceButtonShift] == 2 &&
                 state == buttonRelease && TP->inputs.buttons[43] == 1)
        {
            TP->inputs.buttons[43] = 2;
        }

        if (state == buttonPress && TP->inputs.buttons[button] == 0)
        {
            TP->inputs.buttons[button] = 1;
        }
        else if (state == buttonRelease && TP->inputs.buttons[button] == 1)
        {
            TP->inputs.buttons[button] = 2;
        }
    }
#endif

    if (button == 3 && state == buttonHold)
    {
        TP->skipButton();
    }
    if (button == 3 && state == buttonRelease)
    {
        TP->blockSkip = 0;
    }

    if (state != buttonPress)
    {
        return;
    }
    if (TP->getStatusViewState())
    {
        return;
    }

    if (button == 0)
    {
        TP->AcceptButton();
    }
    else if (button == 3)
    {
        TP->DeclineButton();
    }
}

static void functEncoder(int16_t value)
{
    if (TP->inputs.encoderValue + value > 175)
        TP->inputs.encoderValue = 175;
    else if (TP->inputs.encoderValue + value < -175)
        TP->inputs.encoderValue = -175;
    else
        TP->inputs.encoderValue += value;

    if (TP->inputs.encoderValue >= 175 && TP->inputs.encoderR == 0)
        TP->inputs.encoderR = 1;
    if (TP->inputs.encoderValue <= -175 && TP->inputs.encoderL == 0)
        TP->inputs.encoderL = 1;
}

static void functPads(uint8_t pad, uint8_t state, int16_t velo)
{
    if (state == buttonPress && TP->inputs.pads[pad] == 0)
        TP->inputs.pads[pad] = 1;
    else if (state == buttonRelease && TP->inputs.pads[pad] == 1)
        TP->inputs.pads[pad] = 2;
}

static void functPowerButton(uint8_t state)
{
    if (state == buttonPress && TP->inputs.powerButton == 0)
        TP->inputs.powerButton = 1;
    else if (state == buttonRelease && TP->inputs.powerButton == 1)
        TP->inputs.powerButton = 2;
}
