#include "ampLogarythmicValues.h"
#include "mtMasterEnhancer.h"
#ifdef FEATURE_SYNTH
#include "Synth.h"
#endif
#include "util_math.h"
#include <cmath>
#include <masterParams/masterParams.h>

static const uint16_t framesPlaces[8][4] = {
    { 0 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 1 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 2 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 3 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 4 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 5 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 6 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 7 + 1, 31, 800 / 8 - 3, 391 },
};

const char* const saturationListTexts[5] = { "Hard Clip",
                                             "Soft Clip",
                                             "Saturate",
                                             "Overdrive",
                                             "Drive+"

};

void cMasterParams::initDisplayControls()
{
#ifdef FEATURE_SYNTH
    for (uint8_t trackNo = 0; trackNo < 8 + kSynthParts; trackNo++)
    {
        for (uint8_t i = 0; i < 5; i++)
        {
            levelBarColors[trackNo][i] = display.getTheme().activeBarColors[i];
        }
    }
#else
    for (uint8_t trackNo = 0; trackNo < 8; trackNo++)
    {
        for (uint8_t i = 0; i < 5; i++)
        {
            levelBarColors[trackNo][i] = display.getTheme().activeBarColors[i];
        }
    }
#endif

    strControlProperties prop;
    prop.style = 0;
    prop.value = 0;
    prop.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop);

    strControlProperties prop2;

    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.value = 1;
        prop2.colors = display.getTheme().activeLabelsColors;
        prop2.style = (controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        prop2.x = (800 / 8) * i + (800 / 16);
        prop2.w = 800 / 8 - 6;
        prop2.y = 424;
        prop2.h = 55;
        label[i] = display.createControl<cLabel>(&prop2);
    }

    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.value = 255; // 84
    prop2.style = controlStyleNoTransparency | controlStyleShow;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.value = 0;
        prop2.colors = display.getTheme().topBarLabelColors;
        prop2.style = (controlStyleShow | controlStyleBackground | controlStyleCenterY |
                       controlStyleCenterX | controlStyleFont4);
        prop2.x = (800 / 8) * i + (800 / 16);
        prop2.w = 800 / 8 - 4;
        prop2.y = 32;
        prop2.h = 26;
        trackNameLabel[i] = display.createControl<cLabel>(&prop2);

        prop2.data = nullptr;
        prop2.colors = display.getTheme().activeBarColors;
        prop2.value = 0;
        prop2.x = (800 / 8) * i + 2;
        prop2.y = 64;
        prop2.w = 800 / 16 - 3;
        prop2.style = controlStyleValue_0_100 | controlStyleBackground |
                      (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
                      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
        prop2.h = 359;
        trackVolumeBar[i] = display.createControl<cBar>(&prop2);

        barData[i].changingColor = 1;
        prop2.data = &barData[i];
        prop2.colors = levelBarColors[i];
        prop2.value = 0;
        prop2.x = (800 / 8) * i + 2;
        prop2.y = 29;
        prop2.w = 800 / 8 - 3;
        prop2.style = controlStyleValue_0_100 | controlStyleBackground |
                      (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
                      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
        prop2.h = 394;
        barControl[i] = display.createControl<cBar>(&prop2);
    }

#ifdef FEATURE_SYNTH
    strControlProperties synthBarsProperties;

    for (uint8_t barIdx = 8; barIdx < 8 + kSynthParts; barIdx++)
    {
        barData[barIdx].changingColor = 1;
        synthBarsProperties.data = &barData[barIdx];
        synthBarsProperties.colors = levelBarColors[barIdx];
        synthBarsProperties.value = 0;
        synthBarsProperties.x = 0;
        synthBarsProperties.y = 29;
        synthBarsProperties.w = 800 / 8 - 3;
        synthBarsProperties.style =
          controlStyleValue_0_100 | controlStyleBackground |
          (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
          (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
        synthBarsProperties.h = 394;
        barControl[barIdx] = display.createControl<cBar>(&synthBarsProperties);
    }
#endif

    prop2.value = 0;
    prop2.data = nullptr;
    prop2.text = nullptr;
    prop2.colors = display.getTheme().blankSpaceCoverLabelColors;
    prop2.style = controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop2.x = (800 / 8) * 7 + 2;
    prop2.w = 800 / 8 - 2;
    prop2.y = 29;
    prop2.h = 394;
    blankSpaceCover[0] = display.createControl<cLabel>(&prop2);

    prop2.value = 0;
    prop2.data = nullptr;
    prop2.text = nullptr;
    prop2.colors = display.getTheme().blankSpaceCoverLabelColors;
    prop2.style = controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop2.x = (800 / 8) * 4 + 2;
    prop2.w = (800 / 8) * 1 - 4;
    prop2.y = 64;
    prop2.h = 359;
    blankSpaceCover[1] = display.createControl<cLabel>(&prop2);
    prop2.x = (800 / 8) * 5 + 2;
    blankSpaceCover[2] = display.createControl<cLabel>(&prop2);

    pingpongEnableData.linesCount = 2;
    pingpongEnableData.start = 0;
    pingpongEnableData.length = 2;
    pingpongEnableData.data = (char**)delayPingPongEnable;

    strControlProperties propPingPong;
    propPingPong.style = controlStyleBackground |
                         (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    propPingPong.x = (800 / 8) * (0) + 2;
    propPingPong.y = 29;
    propPingPong.w = 800 / 8 - 3;
    propPingPong.h = 394;
    propPingPong.data = &pingpongEnableData;
    propPingPong.colors = nullptr;
    delayPingpongEnableList = display.createControl<cList>(&propPingPong);

    syncEnableData.linesCount = 2;
    syncEnableData.start = 0;
    syncEnableData.length = 2;
    syncEnableData.data = (char**)delaySyncEnable;

    strControlProperties propSyncEnable;
    propSyncEnable.style =
      controlStyleBackground |
      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    propSyncEnable.x = (800 / 8) * (1) + 2;
    propSyncEnable.y = 29;
    propSyncEnable.w = 800 / 8 - 3;
    propSyncEnable.h = 394;
    propSyncEnable.data = &syncEnableData;
    propSyncEnable.colors = nullptr;
    delaySyncEnableList = display.createControl<cList>(&propSyncEnable);

    syncRateData.linesCount = 14;
    syncRateData.start = 0;
    syncRateData.length = 19;
    syncRateData.data = (char**)delaySyncRates;

    strControlProperties propSyncRate;
    propSyncRate.style = controlStyleBackground |
                         (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    propSyncRate.x = (800 / 8) * (2) + 2;
    propSyncRate.y = 29;
    propSyncRate.w = 800 / 8 - 3;
    propSyncRate.h = 394;
    propSyncRate.data = &syncRateData;
    propSyncRate.colors = nullptr;
    delaySyncRateList = display.createControl<cList>(&propSyncRate);

#ifdef FEATURE_SYNTH
    limiterSidechainData.linesCount = 11 + kSynthParts;
    limiterSidechainData.length = 11 + kSynthParts;
#else
    limiterSidechainData.linesCount = 11;
    limiterSidechainData.length = 11;
#endif
    limiterSidechainData.start = 0;
    limiterSidechainData.data = (char**)limiterSidechainListTxt;

    strControlProperties propLimiterSidechain;
    propLimiterSidechain.style =
      controlStyleBackground |
      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    propLimiterSidechain.x = (800 / 8) * (4) + 2;
    propLimiterSidechain.y = 29;
    propLimiterSidechain.w = 800 / 8 - 3;
    propLimiterSidechain.h = 394;
    propLimiterSidechain.data = &limiterSidechainData;
    propLimiterSidechain.colors = nullptr;
    limiterSidechainList = display.createControl<cList>(&propLimiterSidechain);

    saturationListData.linesCount = 5;
    saturationListData.start = 0;
    saturationListData.length = 5;
    saturationListData.data = (char**)saturationListTexts;

    strControlProperties saturationListProperties;
    saturationListProperties.style =
      controlStyleBackground |
      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    saturationListProperties.x = (800 / 8) * (5) + 2;
    saturationListProperties.y = 29;
    saturationListProperties.w = 800 / 8 - 3;
    saturationListProperties.h = 394;
    saturationListProperties.data = &saturationListData;
    saturationListProperties.colors = nullptr;
    saturationList = display.createControl<cList>(&saturationListProperties);
}

void cMasterParams::showMasterScreen()
{

    display.setControlHide(delayPingpongEnableList);
    display.setControlHide(delaySyncEnableList);
    display.setControlHide(delaySyncRateList);
    display.setControlHide(limiterSidechainList);
    display.setControlShow(saturationList);

    display.refreshControl(saturationList);

    for (uint8_t i = 0; i < 8; i++)
    {
        barData[i].value = display.getTheme().white;
    }

    titleBar->show();
    titleBar->setTitle("Master 1/3");

    display.setControlText(label[0], "Volume");
    display.setControlText(label[1], "Reverb");
    display.setControlText2(label[1], " ");
    display.setControlText(label[2], "Delay");
    display.setControlText2(label[2], " ");
    display.setControlText(label[3], "Limiter");
    display.setControlText2(label[3], "");
    display.setControlText(label[4], "EQ");
    display.setControlText2(label[4], " ");
    display.setControlText(label[5], "Saturation");
    display.setControlText2(label[5], " ");
    display.setControlText(label[6], "Bass Boost");
    display.setControlText(label[7], "Space");
    display.setControlText2(label[7], " ");

    resizeToDefaultMaster();

    display.setControlColors(barControl[3], display.getTheme().activeBarColors);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 1);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);

        if ((i == 0) || (i == 6) || (i == 7))
        {
            display.setControlShow(barControl[i]);
        }
        else
        {
            display.setControlHide(barControl[i]);
        }
        display.refreshControl(barControl[i]);
    }

    display.setControlValue(label[1], 0);
    display.setControlValue(label[2], 0);
    display.setControlValue(label[3], 0);
    display.setControlValue(label[4], 0);

    display.refreshControl(bgLabel);
    display.setControlValue(bgLabel, 255);

    showVolume();
    showSaturationType();
    showBassBoost();
    showStereoBoost();

    frameData.placesCount = 8;
    frameData.startPlace = selectedPlace;
    frameData.places[0] = &framesPlaces[0][0];
    frameData.places[1] = &framesPlaces[1][0];
    frameData.places[2] = &framesPlaces[2][0];
    frameData.places[3] = &framesPlaces[3][0];
    frameData.places[4] = &framesPlaces[4][0];
    frameData.places[5] = &framesPlaces[5][0];
    frameData.places[6] = &framesPlaces[6][0];
    frameData.places[7] = &framesPlaces[7][0];

    activateLabelsBorder();

    resizeBarControl(displayMode::masterValues);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(trackNameLabel[i]);
        display.refreshControl(trackNameLabel[i]);
        display.setControlHide(trackVolumeBar[i]);
        display.refreshControl(trackVolumeBar[i]);
    }

    // empty space covers
    display.setControlPosition(blankSpaceCover[0], (800 / 8) * 1 + 2, -1);
    display.setControlSize(blankSpaceCover[0], (800 / 8) * 4 - 3, -1);
    display.setControlShow(blankSpaceCover[0]);
    display.refreshControl(blankSpaceCover[0]);

    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(blankSpaceCover[2]);

    display.synchronizeRefresh();

    lightUpPadBoard();
}

void cMasterParams::showReverbScreen()
{
    display.setControlHide(saturationList);

    for (uint8_t i = 0; i < 8; i++)
    {
        barData[i].value = display.getTheme().white;
    }

    titleBar->show();
    titleBar->setTitle("Master Reverb");

    display.setControlText(label[0], "Size");
    display.setControlText(label[1], "Damping");
    display.setControlText(label[2], "Predelay");
    display.setControlText(label[3], "Diffusion");
    display.setControlText(label[4], "Line in");
    display.setControlText(label[5], " ");
    display.setControlText2(label[5], " ");
    display.setControlText(label[6], " ");
    display.setControlText2(label[6], " ");
    display.setControlText(label[7], "Back");
    display.setControlText2(label[7], " ");

    resizeToDefaultMaster();
    resizeBarControl(displayMode::masterValues);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 1);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);

        if (i < 5)
        {
            display.setControlShow(barControl[i]);
        }
        else
        {
            display.setControlHide(barControl[i]);
        }
        display.refreshControl(barControl[i]);
    }

    showReverbSize();
    showReverbDamp();
    showReverbPredelay();
    showReverbDiffusion();
    showLineInReverbSend();

    frameData.placesCount = 5;
    frameData.startPlace = selectedPlaceReverb;
    frameData.places[0] = &framesPlaces[0][0];
    frameData.places[1] = &framesPlaces[1][0];
    frameData.places[2] = &framesPlaces[2][0];
    frameData.places[3] = &framesPlaces[3][0];
    frameData.places[4] = &framesPlaces[4][0];
    frameData.places[5] = &framesPlaces[5][0];
    frameData.places[6] = &framesPlaces[6][0];
    frameData.places[7] = &framesPlaces[7][0];

    refreshReverbFrame();

    display.refreshControl(bgLabel);
    display.setControlValue(bgLabel, 255); // 0b10011111

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(trackNameLabel[i]);
        display.refreshControl(trackNameLabel[i]);

        display.setControlHide(trackVolumeBar[i]);
        display.refreshControl(trackVolumeBar[i]);
    }

    // empty space covers
    display.setControlPosition(blankSpaceCover[0], (800 / 8) * 5 + 2, -1);
    display.setControlSize(blankSpaceCover[0], (800 / 8) * 3 - 3, -1);
    display.setControlShow(blankSpaceCover[0]);
    display.refreshControl(blankSpaceCover[0]);

    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(blankSpaceCover[2]);

    display.synchronizeRefresh();

    lightUpPadBoard();
}

void cMasterParams::showDelayScreen()
{
    display.setControlShow(delayPingpongEnableList);
    display.setControlShow(delaySyncEnableList);
    display.setControlShow(delaySyncRateList);
    display.setControlHide(limiterSidechainList);
    display.setControlHide(saturationList);

    display.refreshControl(delayPingpongEnableList);
    display.refreshControl(delaySyncEnableList);
    display.refreshControl(delaySyncRateList);

    for (uint8_t i = 0; i < 8; i++)
    {
        barData[i].value = display.getTheme().white;
    }

    titleBar->show();
    titleBar->setTitle("Master Delay");

    display.setControlText(label[0], "PingPong");
    display.setControlText(label[1], "Sync");
    display.setControlText(label[2], "Rate");
    display.setControlText(label[3], "Time");
    display.setControlText(label[4], "Feedback");
    display.setControlText(label[5], "Filter");
    display.setControlText(label[6], "Line in");
    display.setControlText(label[7], "Back");
    display.setControlText2(label[7], " ");

    resizeToDefaultMaster();
    resizeBarControl(displayMode::masterValues);

    if (mtProject.values.delayParams & 0b01000000)
    {
        display.setControlColors(label[2], display.getTheme().activeLabelsColors);
        display.setControlColors(delaySyncRateList, display.getTheme().activeListColors);
        display.setControlColors(label[3], display.getTheme().inactiveLabelsColors);
        barData[3].value = display.getTheme().white;
        display.setControlColors(barControl[3], levelBarColors[3]);
    }
    else
    {
        display.setControlColors(label[3], display.getTheme().activeLabelsColors);
        barData[3].value = display.getTheme().white;
        display.setControlColors(barControl[3], levelBarColors[3]);
        display.setControlColors(label[2], display.getTheme().inactiveLabelsColors);
        display.setControlColors(delaySyncRateList, display.getTheme().inactiveListColors);
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 1);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);

        if ((i == 3) || (i == 4) || (i == 5) || (i == 6))
        {
            display.setControlShow(barControl[i]);
        }
        else
        {
            display.setControlHide(barControl[i]);
        }
        display.refreshControl(barControl[i]);
    }

    display.refreshControl(bgLabel);

    display.setControlValue(bgLabel, 255); // 0b11111111

    showDelayFeedback();
    showDelayTime();
    showDelayPingPongEnable();
    showDelaySyncEnable();
    showDelayRate();
    showDelayFilterFrequency();
    showLineInDelaySend();

    frameData.placesCount = 7;
    frameData.startPlace = selectedPlaceDelay;
    frameData.places[0] = &framesPlaces[0][0];
    frameData.places[1] = &framesPlaces[1][0];
    frameData.places[2] = &framesPlaces[2][0];
    frameData.places[3] = &framesPlaces[3][0];
    frameData.places[4] = &framesPlaces[4][0];
    frameData.places[5] = &framesPlaces[5][0];
    frameData.places[6] = &framesPlaces[6][0];
    frameData.places[7] = &framesPlaces[7][0];

    refreshDelayFrame();

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(trackVolumeBar[i]);
        display.refreshControl(trackVolumeBar[i]);

        display.setControlHide(trackNameLabel[i]);
        display.refreshControl(trackNameLabel[i]);
    }

    // empty space covers
    display.setControlPosition(blankSpaceCover[0], (800 / 8) * 7 + 2, -1);
    display.setControlSize(blankSpaceCover[0], (800 / 8) * 1 - 3, -1);
    display.setControlShow(blankSpaceCover[0]);
    display.refreshControl(blankSpaceCover[0]);

    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(blankSpaceCover[2]);

    display.synchronizeRefresh();

    lightUpPadBoard();
}

void cMasterParams::showReverbSize()
{
    sprintf(reverbSizeVal, "%d", (int)(mtProject.values.reverb.size * 100));

    uint8_t displaySizeValue = mtProject.values.reverb.size * 100;

    display.setControlValue(barControl[0], displaySizeValue);
    display.refreshControl(barControl[0]);

    display.setControlText2(label[0], reverbSizeVal);
    display.refreshControl(label[0]);
}
void cMasterParams::showReverbDamp()
{
    sprintf(reverbDampVal, "%d", (int)(mtProject.values.reverb.damp * 100));

    uint8_t displayDampValue = mtProject.values.reverb.damp * 100;

    display.setControlValue(barControl[1], displayDampValue);
    display.refreshControl(barControl[1]);

    display.setControlText2(label[1], reverbDampVal);
    display.refreshControl(label[1]);
}
void cMasterParams::showReverbPredelay()
{
    sprintf(reverbPredelayVal, "%d", (int)(mtProject.values.reverb.predelay * 100));

    uint8_t displayPredelayValue = mtProject.values.reverb.predelay * 100;

    display.setControlValue(barControl[2], displayPredelayValue);
    display.refreshControl(barControl[2]);

    display.setControlText2(label[2], reverbPredelayVal);
    display.refreshControl(label[2]);
}
void cMasterParams::showReverbDiffusion()
{
    uint8_t displayDiffusionValue = (uint8_t)map<int>(
      mtProject.values.reverb.diffusion * 100, 0, REVERB_DIFFUSION_MAX * 100, 0, 100);

    sprintf(reverbDiffusionVal, "%d", (int)(displayDiffusionValue));

    display.setControlValue(barControl[3], displayDiffusionValue);
    display.refreshControl(barControl[3]);

    display.setControlText2(label[3], reverbDiffusionVal);
    display.refreshControl(label[3]);
}
//==============================================================================================================
// limiter
void cMasterParams::showLimiterScreen()
{
    display.setControlHide(delayPingpongEnableList);
    display.setControlHide(delaySyncEnableList);
    display.setControlHide(delaySyncRateList);
    display.setControlShow(limiterSidechainList);
    display.setControlHide(saturationList);

    display.refreshControl(limiterSidechainList);

    for (uint8_t i = 0; i < 8; i++)
    {
        barData[i].value = display.getTheme().white;
    }
    barData[3].value = display.getTheme().cyan;

    titleBar->show();
    titleBar->setTitle("Master Limiter");

    display.setControlText(label[0], "LimiterAtt");
    display.setControlText(label[1], "LimiterRls");
    display.setControlText(label[2], "LimiterThr");
    display.setControlText(label[3], "Gain Reduce");
    display.setControlText2(label[3], "");
    display.setControlText(label[4], "Sidechain");
    display.setControlText2(label[4], " ");
    display.setControlText(label[5], " ");
    display.setControlText2(label[5], " ");
    display.setControlText(label[6], " ");
    display.setControlText2(label[6], " ");
    display.setControlText(label[7], "Back");
    display.setControlText2(label[7], " ");

    resizeToDefaultMaster();
    resizeBarControl(displayMode::masterValues);

    uint32_t shadow = (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    uint32_t gradient = (display.getTheme().barShadow > 0 ? controlStyleGradient : 0);

    display.setControlStyle(barControl[3],
                            controlStyleReverse_0_100 | controlStyleBackground | shadow | gradient);
    display.setControlColors(barControl[3], levelBarColors[3]);
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 1);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);

        if (i < 4)
        {
            display.setControlShow(barControl[i]);
        }
        else
        {
            display.setControlHide(barControl[i]);
        }
        display.refreshControl(barControl[i]);
    }

    display.setControlValue(label[3], 0);
    display.refreshControl(bgLabel);
    display.setControlValue(bgLabel, 255); // 0b10111111

    showLimiterAttack();
    showLimiterRelease();
    showLimiterThreshold();
    showLimiterGainReduce();
    showLimiterSidechain();

    frameData.placesCount = 5;
    frameData.startPlace = selectedPlaceLimiter;
    frameData.places[0] = &framesPlaces[0][0];
    frameData.places[1] = &framesPlaces[1][0];
    frameData.places[2] = &framesPlaces[2][0];
    frameData.places[3] = &framesPlaces[3][0];
    frameData.places[4] = &framesPlaces[4][0];
    frameData.places[5] = &framesPlaces[5][0];
    frameData.places[6] = &framesPlaces[6][0];
    frameData.places[7] = &framesPlaces[7][0];

    refreshLimiterFrame();

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(trackNameLabel[i]);
        display.refreshControl(trackNameLabel[i]);
        display.setControlHide(trackVolumeBar[i]);
        display.refreshControl(trackVolumeBar[i]);
    }

    // empty space covers
    display.setControlPosition(blankSpaceCover[0], (800 / 8) * 5 + 2, -1);
    display.setControlSize(blankSpaceCover[0], (800 / 1) * 3 - 3, -1);
    display.setControlShow(blankSpaceCover[0]);
    display.refreshControl(blankSpaceCover[0]);

    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(blankSpaceCover[2]);

    display.synchronizeRefresh();

    lightUpPadBoard();
}

void cMasterParams::showLimiterAttack()
{
    constexpr uint8_t LIMITER_ATTACK_FIELD_IDX = 0;

    sprintf(limitAttackVal, "%.3fs", (float)(mtProject.values.limiterAttack / 1000.0f));

    uint8_t barValue = (mtProject.values.limiterAttack * 100) / LIMITER_ATTACK_MAX;

    display.setControlValue(barControl[LIMITER_ATTACK_FIELD_IDX], barValue);
    display.refreshControl(barControl[LIMITER_ATTACK_FIELD_IDX]);

    display.setControlText2(label[LIMITER_ATTACK_FIELD_IDX], limitAttackVal);
    display.refreshControl(label[LIMITER_ATTACK_FIELD_IDX]);
}

void cMasterParams::showLimiterRelease()
{
    constexpr uint8_t LIMITER_RELEASE_FIELD_IDX = 1;

    sprintf(limitReleaseVal, "%.3fs", (float)(mtProject.values.limiterRelease / 1000.0f));

    uint8_t barValue = (mtProject.values.limiterRelease * 100) / LIMITER_RELEASE_MAX;

    display.setControlValue(barControl[LIMITER_RELEASE_FIELD_IDX], barValue);
    display.refreshControl(barControl[LIMITER_RELEASE_FIELD_IDX]);

    display.setControlText2(label[LIMITER_RELEASE_FIELD_IDX], limitReleaseVal);
    display.refreshControl(label[LIMITER_RELEASE_FIELD_IDX]);
}

void cMasterParams::showLimiterThreshold()
{
    constexpr uint8_t LIMITER_THRESHOLD_FIELD_IDX = 2;

    uint8_t percentValue = (mtProject.values.limiterThreshold * 100) / LIMITER_THRESHOLD_MAX;

    float thresholdInDb =
      20 * log10f((float)limiterThresholdValues[percentValue] / (float)LIMITER_THRESHOLD_MAX);

    sprintf(limitThresholdVal, "%.2f dB", thresholdInDb);

    display.setControlValue(barControl[LIMITER_THRESHOLD_FIELD_IDX], percentValue);
    display.setControlShow(barControl[LIMITER_THRESHOLD_FIELD_IDX]);
    display.refreshControl(barControl[LIMITER_THRESHOLD_FIELD_IDX]);

    display.setControlText2(label[LIMITER_THRESHOLD_FIELD_IDX], limitThresholdVal);
    display.refreshControl(label[LIMITER_THRESHOLD_FIELD_IDX]);
}

void cMasterParams::showLimiterGainReduce()
{
    constexpr uint8_t GAIN_REDUCE_FIELD_IDX = 3;

    //	float gainReduceInDb = 10*log10f(gainReduce.value);
    //	uint8_t barValue = (gainReduce.value == 0.0f) ? 0 : map(gainReduceInDb,-20.0f, 0.0f, 0.0f,
    // 100.0f);

    //	sprintf(gainReduceVal,"%.2f dB",gainReduceInDb);

    uint8_t barValue = gainReduce.value * 100;

    display.setControlValue(barControl[GAIN_REDUCE_FIELD_IDX], barValue);
    display.setControlShow(barControl[GAIN_REDUCE_FIELD_IDX]);
    display.refreshControl(barControl[GAIN_REDUCE_FIELD_IDX]);

    //	display.setControlText2(label[GAIN_REDUCE_FIELD_IDX], gainReduceVal);
    //	display.refreshControl(label[GAIN_REDUCE_FIELD_IDX]);
}

void cMasterParams::showLimiterSidechain()
{
    display.setControlValue(limiterSidechainList, mtProject.values.limiterSidechain);
    display.refreshControl(limiterSidechainList);

    display.setControlText2(label[4], limiterSidechainListTxt[mtProject.values.limiterSidechain]);
    display.refreshControl(label[4]);
}

// EQ

void cMasterParams::showEqualizerScreen()
{
    display.setControlHide(delayPingpongEnableList);
    display.setControlHide(delaySyncEnableList);
    display.setControlHide(delaySyncRateList);
    display.setControlHide(limiterSidechainList);
    display.setControlHide(saturationList);

    display.refreshControl(delayPingpongEnableList);
    display.refreshControl(delaySyncEnableList);
    display.refreshControl(delaySyncRateList);
    display.refreshControl(limiterSidechainList);
    display.refreshControl(saturationList);

    for (uint8_t i = 0; i < 8; i++)
    {
        barData[i].value = display.getTheme().white;
    }

    titleBar->show();
    titleBar->setTitle("Master EQ");

    display.setControlText(label[0], "115 Hz");
    display.setControlText(label[1], "330 Hz");
    display.setControlText(label[2], "990 Hz");
    display.setControlText(label[3], "3000 Hz");
    display.setControlText(label[4], "9900 Hz");
    display.setControlText(label[5], " ");
    display.setControlText2(label[5], " ");
    display.setControlText(label[6], " ");
    display.setControlText2(label[6], " ");
    display.setControlText(label[7], "Back");
    display.setControlText2(label[7], " ");

    resizeToDefaultMaster();
    resizeBarControl(displayMode::masterValues);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 1);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);

        if (i < 5)
        {
            display.setControlShow(barControl[i]);
        }
        else
        {
            display.setControlHide(barControl[i]);
        }
        display.refreshControl(barControl[i]);
    }

    display.refreshControl(bgLabel);
    display.setControlValue(bgLabel, 255); // 0b10111111

    for (uint8_t band = 0; band < 5; band++)
    {
        showEqualizerBand(band);
    }

    frameData.placesCount = 5;
    frameData.startPlace = selectedPlaceLimiter;
    frameData.places[0] = &framesPlaces[0][0];
    frameData.places[1] = &framesPlaces[1][0];
    frameData.places[2] = &framesPlaces[2][0];
    frameData.places[3] = &framesPlaces[3][0];
    frameData.places[4] = &framesPlaces[4][0];
    frameData.places[5] = &framesPlaces[5][0];
    frameData.places[6] = &framesPlaces[6][0];
    frameData.places[7] = &framesPlaces[7][0];

    refreshEqualizerFrame();

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(trackNameLabel[i]);
        display.refreshControl(trackNameLabel[i]);
        display.setControlHide(trackVolumeBar[i]);
        display.refreshControl(trackVolumeBar[i]);
    }

    // empty space covers
    display.setControlPosition(blankSpaceCover[0], (800 / 8) * 5 + 2, -1);
    display.setControlSize(blankSpaceCover[0], (800 / 1) * 3 - 3, -1);
    display.setControlShow(blankSpaceCover[0]);
    display.refreshControl(blankSpaceCover[0]);

    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(blankSpaceCover[2]);

    display.synchronizeRefresh();

    lightUpPadBoard();
}
void cMasterParams::showEqualizerBand(uint8_t n)
{
    float BAND_ADJUSTMENT_MIN_IN_DB = -12.0f;
    float BAND_ADJUSTMENT_MAX_IN_DB = 12.0f;

    float selectedBandIndB = map(mtProject.values.eqBandAdjustment[n],
                                 BAND_ADJUSTMENT_MIN,
                                 BAND_ADJUSTMENT_MAX,
                                 BAND_ADJUSTMENT_MIN_IN_DB,
                                 BAND_ADJUSTMENT_MAX_IN_DB);

    if ((selectedBandIndB < 0.0f) && (selectedBandIndB >= -0.009f))
        selectedBandIndB = 0.0f;

    sprintf(eqBandAdjustment[n], "%.2f dB", selectedBandIndB);

    uint8_t barValue = map(
      mtProject.values.eqBandAdjustment[n], BAND_ADJUSTMENT_MIN, BAND_ADJUSTMENT_MAX, 0.0f, 100.0f);

    display.setControlValue(barControl[n], barValue);
    display.refreshControl(barControl[n]);

    display.setControlText2(label[n], eqBandAdjustment[n]);
    display.refreshControl(label[n]);
}

//////////////////////////////////////////////////////////////////////////////

void cMasterParams::activateLabelsBorder()
{
    if (selectedPlace > frameData.placesCount - 1)
        return;

    display.setControlValue(frameControl, selectedPlace);
    display.setControlShow(frameControl);
    display.refreshControl(frameControl);
}

void cMasterParams::refreshDelayFrame()
{
    if (selectedPlaceDelay > frameData.placesCount - 1)
        return;

    display.setControlValue(frameControl, selectedPlaceDelay);
    display.setControlShow(frameControl);
    display.refreshControl(frameControl);
}

void cMasterParams::refreshReverbFrame()
{
    if (selectedPlaceReverb > frameData.placesCount - 1)
        return;

    display.setControlValue(frameControl, selectedPlaceReverb);
    display.setControlShow(frameControl);
    display.refreshControl(frameControl);
}

void cMasterParams::refreshLimiterFrame()
{
    if (selectedPlaceLimiter > frameData.placesCount - 1)
        return;

    display.setControlValue(frameControl, selectedPlaceLimiter);
    display.setControlShow(frameControl);
    display.refreshControl(frameControl);
}

void cMasterParams::refreshEqualizerFrame()
{
    if (selectedPlaceEqualizer > frameData.placesCount - 1)
        return;

    display.setControlValue(frameControl, selectedPlaceEqualizer);
    display.setControlShow(frameControl);
    display.refreshControl(frameControl);
}

void cMasterParams::changeLabelText(uint8_t labelIdx, const char* text)
{
    display.setControlText(label[labelIdx], text);
    display.refreshControl(label[labelIdx]);
}

//==============================================================================================================

void cMasterParams::resizeToDefaultConfig()
{
    display.setControlPosition(label[0], (800 / 8) * 0 + (800 / 8), -1);
    display.setControlSize(label[0], 800 / 4 - 6, -1);

    display.setControlHide(label[1]);

    display.setControlPosition(label[2], (800 / 8) * 2 + (800 / 8), -1);
    display.setControlSize(label[2], 800 / 4 - 6, -1);

    display.setControlHide(label[3]);

    display.setControlPosition(label[4], (800 / 8) * 4 + (800 / 8), -1);
    display.setControlSize(label[4], 800 / 4 - 6, -1);

    display.setControlHide(label[5]);

    display.setControlPosition(label[6], (800 / 8) * 6 + (800 / 8), -1);
    display.setControlSize(label[6], 800 / 4 - 6, -1);

    display.setControlHide(label[7]);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlColors(label[i], display.getTheme().activeLabelsColors);
    }
}

void cMasterParams::resizeBarControl(displayMode val)
{
    uint32_t shadow = (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    uint32_t gradient = (display.getTheme().barShadow > 0 ? controlStyleGradient : 0);

    if (val == displayMode::masterValues)
    {
        if (!isLimiterScreen)
        {
            display.setControlStyle(
              barControl[3], controlStyleValue_0_100 | controlStyleBackground | shadow | gradient);
            display.refreshControl(barControl[3]);
        }

        for (uint8_t i = 0; i < 8; i++)
        {
            display.setControlPosition(barControl[i], (800 / 8) * i + 2, 29);
            display.setControlSize(barControl[i], 800 / 8 - 3, 394);
            display.refreshControl(barControl[i]);
        }

#ifdef FEATURE_SYNTH
        for (uint8_t i = 8; i < 8 + kSynthParts; i++)
        {
            display.setControlHide(barControl[i]);
            display.refreshControl(barControl[i]);
        }
#endif
    }
    else if (val == displayMode::mixer)
    {
        display.setControlStyle(
          barControl[3], controlStyleValue_0_100 | controlStyleBackground | shadow | gradient);
        display.setControlShow(barControl[3]);
        display.refreshControl(barControl[3]);

        for (uint8_t i = 0; i < 8; i++)
        {
            display.setControlPosition(trackVolumeBar[i], (800 / 8) * i + 2, 64);
            display.setControlSize(trackVolumeBar[i], 800 / 16 - 3, 359);
            display.refreshControl(trackVolumeBar[i]);

            display.setControlPosition(barControl[i], (800 / 8) * i + 2 + 800 / 16, 64);
            display.setControlSize(barControl[i], 800 / 16 - 3, 359);
            display.refreshControl(barControl[i]);
        }
#ifdef FEATURE_SYNTH
        for (uint8_t i = 8; i < 8 + kSynthParts; i++)
        {
            display.setControlHide(barControl[i]);
            display.refreshControl(barControl[i]);
        }
#endif
    }
    else if (val == displayMode::mixerDelayReverb)
    {
        display.setControlStyle(
          barControl[3], controlStyleValue_0_100 | controlStyleBackground | shadow | gradient);
        display.setControlShow(barControl[3]);
        display.refreshControl(barControl[3]);
#ifdef FEATURE_SYNTH
        for (uint8_t i = 0; i < 7; i++)
        {
            static constexpr int kGlobalMixerBars = 4;
            if (i < kGlobalMixerBars)
            {
                display.setControlPosition(trackVolumeBar[i], (800 / 8) * i + 2, 64);
                display.setControlSize(trackVolumeBar[i], 32, 359);
                display.refreshControl(trackVolumeBar[i]);

                display.setControlPosition(barControl[i], (800 / 8) * i + 2 + 32, 64);
                display.setControlSize(barControl[i], 32, 359);
                display.refreshControl(barControl[i]);

                display.setControlPosition(barControl[i + 4], (800 / 8) * i + 2 + 64, 64);
                display.setControlSize(barControl[i + 4], 32, 359);
                display.refreshControl(barControl[i + 4]);
            }
            else
            {
                display.setControlPosition(trackVolumeBar[i], (800 / 8) * i + 2, 64);
                display.setControlSize(trackVolumeBar[i], 800 / 16 - 3, 359);
                display.refreshControl(trackVolumeBar[i]);

                static constexpr int kSynthBarOffset = kGlobalMixerBars * 2;
                int synthIdx = i - kGlobalMixerBars;

                display.setControlPosition(
                  barControl[kSynthBarOffset + synthIdx], (800 / 8) * i + 2 + 800 / 16, 64);
                display.setControlSize(barControl[kSynthBarOffset + synthIdx], 800 / 16 - 3, 359);
                display.refreshControl(barControl[kSynthBarOffset + synthIdx]);
            }
        }
#else
        for (uint8_t i = 0; i < 4; i++)
        {
            display.setControlPosition(trackVolumeBar[i], (800 / 8) * i + 2, 64);
            display.setControlSize(trackVolumeBar[i], 32, 359);
            display.refreshControl(trackVolumeBar[i]);

            display.setControlPosition(barControl[i], (800 / 8) * i + 2 + 32, 64);
            display.setControlSize(barControl[i], 32, 359);
            display.refreshControl(barControl[i]);

            display.setControlPosition(barControl[i + 4], (800 / 8) * i + 2 + 64, 64);
            display.setControlSize(barControl[i + 4], 32, 359);
            display.refreshControl(barControl[i + 4]);
        }
#endif
    }
}

void cMasterParams::resizeToDefaultMaster()
{
    display.setControlPosition(label[0], (800 / 8) * 0 + (800 / 16), -1);
    display.setControlSize(label[0], 800 / 8 - 6, -1);

    display.setControlPosition(label[2], (800 / 8) * 2 + (800 / 16), -1);
    display.setControlSize(label[2], 800 / 8 - 6, -1);

    display.setControlPosition(label[4], (800 / 8) * 4 + (800 / 16), -1);
    display.setControlSize(label[4], 800 / 8 - 6, -1);

    display.setControlPosition(label[6], (800 / 8) * 6 + (800 / 16), -1);
    display.setControlSize(label[6], 800 / 8 - 6, -1);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlColors(label[i], display.getTheme().activeLabelsColors);
    }
}

void cMasterParams::resizeToSmallConfig(uint8_t labelIdx)
{
    display.setControlPosition(label[labelIdx], (800 / 8) * labelIdx + (800 / 16), 452);
    display.setControlSize(label[labelIdx], 800 / 8 - 6, 59);

    display.setControlShow(label[labelIdx + 1]);
}

void cMasterParams::showVolume()
{
    if (displayType != displayMode::masterValues)
    {
        return;
    }

    if (isDelayScreen || isReverbScreen || isLimiterScreen || isEqualizerScreen)
    {
        return;
    }

    float masterVolumeInDecybels = map((float)(mtConfig.audioCodecConfig.volume / 100.0f),
                                       0.0f,
                                       1.0f,
                                       MIN_ANALOG_VOLUME_IN_DB,
                                       MAX_ANALOG_VOLUME_IN_DB);

    if (mtConfig.audioCodecConfig.volume == MASTER_VOLUME_DEFAULT)
    {
        masterVolumeInDecybels = 0;
    }

    sprintf(volumeVal, "%0.2f dB", masterVolumeInDecybels);

    display.setControlValue(barControl[0], mtConfig.audioCodecConfig.volume);
    display.refreshControl(barControl[0]);

    display.setControlText2(label[0], volumeVal);
    display.refreshControl(label[0]);
}

void cMasterParams::showSaturationType()
{
    display.setControlValue(saturationList, mtProject.values.saturationType);
    display.refreshControl(saturationList);

    display.setControlText2(label[5], saturationListTexts[mtProject.values.saturationType]);
    display.refreshControl(label[5]);
}

void cMasterParams::showBassBoost()
{
    sprintf(bassBoostVal, "%d", mtProject.values.bassBoost);

    display.setControlValue(barControl[6], mtProject.values.bassBoost);
    display.refreshControl(barControl[6]);

    display.setControlText2(label[6], bassBoostVal);
    display.refreshControl(label[6]);
}

void cMasterParams::showStereoBoost()
{
    uint8_t barValue = (uint8_t)map<int>(mtProject.values.stereoBoost, 0, 8, 0, 100);

    sprintf(stereoBoostVal, "%d", mtProject.values.stereoBoost);

    display.setControlValue(barControl[7], barValue);
    display.refreshControl(barControl[7]);

    display.setControlText2(label[7], stereoBoostVal);
    display.refreshControl(label[7]);
}

// delay

void cMasterParams::showDelayPingPongEnable()
{
    bool enable = (mtProject.values.delayParams & (0b10000000));

    if (enable)
    {
        display.setControlValue(delayPingpongEnableList, 0);
        display.refreshControl(delayPingpongEnableList);

        display.setControlText2(label[0], delayPingPongEnable[0]);
        display.refreshControl(label[0]);
    }
    else
    {
        display.setControlValue(delayPingpongEnableList, 1);
        display.refreshControl(delayPingpongEnableList);

        display.setControlText2(label[0], delayPingPongEnable[1]);
        display.refreshControl(label[0]);
    }
}
void cMasterParams::showDelaySyncEnable()
{
    bool enable = (mtProject.values.delayParams & (0b01000000));

    if (enable)
    {
        display.setControlValue(delaySyncEnableList, 0);
        display.refreshControl(delaySyncEnableList);

        display.setControlText2(label[1], delaySyncEnable[0]);
        display.refreshControl(label[1]);

        display.setControlColors(label[2], display.getTheme().activeLabelsColors);
        display.setControlColors(delaySyncRateList, display.getTheme().activeListColors);
        display.setControlColors(label[3], display.getTheme().inactiveLabelsColors);
        barData[3].value = display.getTheme().grey_22;
        display.setControlColors(barControl[3], levelBarColors[3]);

        display.refreshControl(label[2]);
        display.refreshControl(delaySyncRateList);
        display.refreshControl(label[3]);
        display.refreshControl(barControl[3]);
    }
    else
    {
        display.setControlValue(delaySyncEnableList, 1);
        display.refreshControl(delaySyncEnableList);

        display.setControlText2(label[1], delaySyncEnable[1]);
        display.refreshControl(label[1]);

        display.setControlColors(label[3], display.getTheme().activeLabelsColors);
        barData[3].value = display.getTheme().white;
        display.setControlColors(barControl[3], levelBarColors[3]);
        display.setControlColors(label[2], display.getTheme().inactiveLabelsColors);
        display.setControlColors(delaySyncRateList, display.getTheme().inactiveListColors);

        display.refreshControl(label[2]);
        display.refreshControl(delaySyncRateList);
        display.refreshControl(label[3]);
        display.refreshControl(barControl[3]);
    }

    display.synchronizeRefresh();
}
void cMasterParams::showDelayRate()
{
    uint8_t rateIdx = (mtProject.values.delayParams & (0b00111111));
    // The index is six bits of a project value, and nothing checks it when a project is loaded.
    // The delay uses its last rate for such a value, so show that one.
    constexpr uint8_t kLastRateIdx = sizeof(delaySyncRates) / sizeof(delaySyncRates[0]) - 1;
    if (rateIdx > kLastRateIdx)
        rateIdx = kLastRateIdx;

    display.setControlValue(delaySyncRateList, rateIdx);
    display.refreshControl(delaySyncRateList);

    display.setControlText2(label[2], delaySyncRates[rateIdx]);
    display.refreshControl(label[2]);
}
void cMasterParams::showDelayFeedback()
{
    sprintf(delayFeedbackVal, "%d", mtProject.values.delayFeedback);

    uint8_t displayFeedbackTime = (uint8_t)map<int>(
      mtProject.values.delayFeedback, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX, 0, 100);

    display.setControlValue(barControl[4], displayFeedbackTime);
    display.refreshControl(barControl[4]);

    display.setControlText2(label[4], delayFeedbackVal);
    display.refreshControl(label[4]);
}

void cMasterParams::showDelayTime()
{
    sprintf(delayTimeVal, "%d ms", mtProject.values.delayTime);

    uint8_t displayDelayTime = 0;

    if (mtProject.values.delayTime > 500)
        displayDelayTime =
          (uint8_t)map<int>(mtProject.values.delayTime, 500, DELAY_TIME_MAX, 50, 100);
    else if (mtProject.values.delayTime > 100)
        displayDelayTime = (uint8_t)map<int>(mtProject.values.delayTime, 100, 500, 25, 50);
    else
        displayDelayTime = (uint8_t)map<int>(mtProject.values.delayTime, 0, 100, 0, 25);

    display.setControlValue(barControl[3], displayDelayTime);
    display.refreshControl(barControl[3]);

    display.setControlText2(label[3], delayTimeVal);
    display.refreshControl(label[3]);
}

void cMasterParams::showDelayFilterFrequency()
{
    sprintf(delayFilterFreqVal, "%d Hz", mtProject.values.delayFilterFrequency);

    int displayedValue = map<int>(
      mtProject.values.delayFilterFrequency, DELAY_FILTER_FREQ_MIN, DELAY_FILTER_FREQ_MAX, 0, 100);

    display.setControlValue(barControl[5], displayedValue);
    display.refreshControl(barControl[5]);

    display.setControlText2(label[5], delayFilterFreqVal);
    display.refreshControl(label[5]);
}

// mixer
void cMasterParams::showMixerScreen()
{
    display.setControlHide(delayPingpongEnableList);
    display.setControlHide(delaySyncEnableList);
    display.setControlHide(delaySyncRateList);
    display.setControlHide(limiterSidechainList);
    display.setControlHide(saturationList);

    titleBar->show();
#ifdef FEATURE_SYNTH
    titleBar->setTitle("Sample Track Mixer 2/3");
#else
    titleBar->setTitle("Track Mixer 2/3");
#endif

    if (isSolo)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            sprintf(mixerLabel[i], "%s", soloTrack == i ? (char*)"Unsolo" : (char*)"Solo");
        }
    }
    else
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            sprintf(
              mixerLabel[i], "%s", mtProject.values.trackMute[i] ? (char*)"Unmute" : (char*)"Mute");
        }
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        mtProject.values.TrackNames[i][7] = 0;

        showLevelBar(i, &trackLevel[i]);
        showTrackVolumeBar(i);

        display.setControlText(trackNameLabel[i], &mtProject.values.TrackNames[i][0]);
        display.setControlShow(trackNameLabel[i]);
        display.refreshControl(trackNameLabel[i]);

        display.setControlValue(label[i], 1);
        display.setControlColors(label[i],
                                 mtProject.values.trackMute[i]
                                   ? display.getTheme().inactiveLabelsColors
                                   : display.getTheme().activeLabelsColors);

        display.setControlText2(label[i], mixerLabel[i]);
        display.refreshControl(label[i]);
    }

    display.refreshControl(bgLabel);
    display.setControlValue(bgLabel, 255);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    resizeBarControl(displayMode::mixer);

    display.setControlHide(blankSpaceCover[0]);
    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(blankSpaceCover[2]);

    display.synchronizeRefresh();

    lightUpPadBoard();
}

void cMasterParams::showMixerDelayReverbScreen()
{
    display.setControlHide(delayPingpongEnableList);
    display.setControlHide(delaySyncEnableList);
    display.setControlHide(delaySyncRateList);
    display.setControlHide(limiterSidechainList);
    display.setControlHide(saturationList);

    titleBar->show();
    titleBar->setTitle("Global Mixer 3/3");

    char* mixerLabelFormat = (char*)"%s";

    if (isSolo)
    {
        char* unsoloText = (char*)"Unsolo";
        char* soloText = (char*)"Solo";

        for (uint8_t i = 0; i < screenPositionCount; i++)
        {
            if (i == delayVolumeScreenPosition)
            {
                sprintf(mixerLabel[i],
                        mixerLabelFormat,
                        soloTrack == delayIsSolo ? unsoloText : soloText);
            }
            else if (i == reverbVolumeScreenPosition)
            {
                sprintf(mixerLabel[i],
                        mixerLabelFormat,
                        soloTrack == reverbIsSolo ? unsoloText : soloText);
            }
            else if (i == dryMixVolumeScreenPosition)
            {
                sprintf(mixerLabel[i],
                        mixerLabelFormat,
                        soloTrack == dryMixIsSolo ? unsoloText : soloText);
            }
            else if (i == lineInVolumeScreenPosition)
            {
                sprintf(mixerLabel[i],
                        mixerLabelFormat,
                        soloTrack == lineInIsSolo ? unsoloText : soloText);
            }
#ifdef FEATURE_SYNTH
            else if (i >= synthVolumeScreenPositionStart && i <= synthVolumeScreenPositionEnd)
            {
                sprintf(mixerLabel[i], mixerLabelFormat, " ");
            }
#endif
        }
    }
    else
    {
        char* unmuteText = (char*)"Unmute";
        char* muteText = (char*)"Mute";

        for (uint8_t i = 0; i < screenPositionCount; i++)
        {
            if (i == delayVolumeScreenPosition)
            {
                sprintf(mixerLabel[i],
                        mixerLabelFormat,
                        mtProject.values.delayMute ? unmuteText : muteText);
            }
            else if (i == reverbVolumeScreenPosition)
            {
                sprintf(mixerLabel[i],
                        mixerLabelFormat,
                        mtProject.values.reverbMute ? unmuteText : muteText);
            }
            else if (i == dryMixVolumeScreenPosition)
            {
                sprintf(mixerLabel[i],
                        mixerLabelFormat,
                        mtProject.values.dryMixMute ? unmuteText : muteText);
            }
            else if (i == lineInVolumeScreenPosition)
            {
                sprintf(mixerLabel[i],
                        mixerLabelFormat,
                        mtProject.values.lineInMute ? unmuteText : muteText);
            }
#ifdef FEATURE_SYNTH
            else if (i >= synthVolumeScreenPositionStart && i <= synthVolumeScreenPositionEnd)
            {
                sprintf(mixerLabel[i], mixerLabelFormat, " ");
            }
#endif
        }
    }

    for (uint8_t i = screenPositionCount; i < 8; i++)
    {
        sprintf(mixerLabel[i], " ");
    }

    if (displayType == displayMode::mixerDelayReverb)
    {
        showLevelBar(delayVolumeScreenPosition, &delayLevel[0]);
        showLevelBar(delayVolumeScreenPosition + 4, &delayLevel[1]);
        showLevelBar(reverbVolumeScreenPosition, &reverbLevel[0]);
        showLevelBar(reverbVolumeScreenPosition + 4, &reverbLevel[1]);
        showLevelBar(dryMixVolumeScreenPosition, &dryMixLevel[0]);
        showLevelBar(dryMixVolumeScreenPosition + 4, &dryMixLevel[1]);
        showLevelBar(lineInVolumeScreenPosition, &lineInLevel[0]);
        showLevelBar(lineInVolumeScreenPosition + 4, &lineInLevel[1]);
#ifdef FEATURE_SYNTH
        static constexpr int kSynthOffset = 8;
        for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
        {
            showLevelBar(kSynthOffset + synthIdx, &synthLevel[synthIdx]);
        }
#endif

        showDelayVolumeBar();
        showReverbVolumeBar();
        showDryMixVolumeBar();
        showLineInVolumeBar();

        display.setControlText(trackNameLabel[delayVolumeScreenPosition], "Delay");
        display.setControlText(trackNameLabel[reverbVolumeScreenPosition], "Reverb");
        display.setControlText(trackNameLabel[dryMixVolumeScreenPosition], "Dry Mix");
        display.setControlText(trackNameLabel[lineInVolumeScreenPosition], "Line In");
#ifdef FEATURE_SYNTH
        for (int labelIdx = synthVolumeScreenPositionStart;
             labelIdx <= synthVolumeScreenPositionEnd;
             labelIdx++)
        {
            int synthIdx = labelIdx - synthVolumeScreenPositionStart;

            snprintf(&synthVoiceLabel[synthIdx][0],
                     sizeof(synthVoiceLabel[synthIdx]),
                     "Synth %d",
                     synthIdx + 1);

            display.setControlText(trackNameLabel[labelIdx], &synthVoiceLabel[synthIdx][0]);
            display.setControlColors(label[labelIdx], display.getTheme().activeLabelsColors);

            showSynthGain(synthIdx);
        }
#endif

        for (uint8_t labelIdx = 0; labelIdx < screenPositionCount; labelIdx++)
        {
            display.setControlShow(trackNameLabel[labelIdx]);
            display.refreshControl(trackNameLabel[labelIdx]);
        }

        for (uint8_t i = screenPositionCount; i < 8; i++)
        {
            display.setControlHide(trackNameLabel[i]);
            display.refreshControl(trackNameLabel[i]);

            display.setControlHide(trackVolumeBar[i]);
            display.refreshControl(trackVolumeBar[i]);

            display.setControlText(label[i], "");
        }
    }

    display.setControlColors(label[delayVolumeScreenPosition],
                             mtProject.values.delayMute ? display.getTheme().inactiveLabelsColors
                                                        : display.getTheme().activeLabelsColors);
    display.setControlColors(label[reverbVolumeScreenPosition],
                             mtProject.values.reverbMute ? display.getTheme().inactiveLabelsColors
                                                         : display.getTheme().activeLabelsColors);
    display.setControlColors(label[dryMixVolumeScreenPosition],
                             mtProject.values.dryMixMute ? display.getTheme().inactiveLabelsColors
                                                         : display.getTheme().activeLabelsColors);
    display.setControlColors(label[lineInVolumeScreenPosition],
                             mtProject.values.lineInMute ? display.getTheme().inactiveLabelsColors
                                                         : display.getTheme().activeLabelsColors);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 1);
        display.setControlText2(label[i], mixerLabel[i]);
        display.refreshControl(label[i]);
    }

    display.setControlValue(bgLabel, 255); // 0b00011111
    display.refreshControl(bgLabel);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    resizeBarControl(displayMode::mixerDelayReverb);

    // empty space covers
#ifdef FEATURE_SYNTH
    display.setControlPosition(blankSpaceCover[0], (800 / 8) * 7 + 2, -1);
    display.setControlSize(blankSpaceCover[0], (800 / 8) * 1 - 4, -1);
#else
    display.setControlPosition(blankSpaceCover[0], (800 / 8) * 4 + 2, -1);
    display.setControlSize(blankSpaceCover[0], (800 / 8) * 4 - 4, -1);
#endif
    display.setControlShow(blankSpaceCover[0]);
    display.refreshControl(blankSpaceCover[0]);

    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(blankSpaceCover[2]);
    display.refreshControl(blankSpaceCover[1]);
    display.refreshControl(blankSpaceCover[2]);

    display.synchronizeRefresh();

    lightUpPadBoard();
}

void cMasterParams::showLevelBar(uint8_t n, strTrackLevel* level)
{
    uint32_t one_true_red = display.getTheme().red;
    uint32_t one_true_green = display.getTheme().green;
    constexpr uint8_t RED_COLOR_LEVEL = 95;
    constexpr uint8_t LEVEL_TRESHOLD_UP = 80;
    constexpr uint8_t LEVEL_TRESHOLD_LOW = 60;

    if (level->value < RED_COLOR_LEVEL)
    {
        if (level->redColorTimer < 350)
        {
            barData[n].value = one_true_red;
        }
        else
        {
            if (level->value > LEVEL_TRESHOLD_UP)
            {
                uint8_t green = (uint8_t)map<int>(level->value,
                                                  LEVEL_TRESHOLD_UP,
                                                  RED_COLOR_LEVEL,
                                                  ((one_true_green & 0xff00) >> 8),
                                                  0);
                barData[n].value = (one_true_red & 0xff0000) | (green << 8);
            }
            else if (level->value > LEVEL_TRESHOLD_LOW)
            {
                uint8_t red = (uint8_t)map<int>(level->value,
                                                LEVEL_TRESHOLD_LOW,
                                                LEVEL_TRESHOLD_UP,
                                                0,
                                                ((one_true_red & 0xff0000) >> 16));
                barData[n].value = (red << 16) | (one_true_green & 0xff00);
            }
            else
            {
                barData[n].value = one_true_green;
            }
        }

        if (level->redColorTimer > 3000000)
            level->redColorTimer = 351;
    }
    else
    {
        level->redColorTimer = 0;
        barData[n].value = one_true_red;
    }

    if (displayType == displayMode::mixerDelayReverb)
    {
        if ((n == delayVolumeScreenPosition) || (n == delayVolumeScreenPosition + 4))
        {
            if (mtProject.values.delayMute)
                barData[n].value = display.getTheme().grey_22;
        }
        else if ((n == reverbVolumeScreenPosition) || (n == reverbVolumeScreenPosition + 4))
        {
            if (mtProject.values.reverbMute)
                barData[n].value = display.getTheme().grey_22;
        }
        else if ((n == dryMixVolumeScreenPosition) || (n == dryMixVolumeScreenPosition + 4))
        {
            if (mtProject.values.dryMixMute)
                barData[n].value = display.getTheme().grey_22;
        }
        else if ((n == lineInVolumeScreenPosition) || (n == lineInVolumeScreenPosition + 4))
        {
            if (mtProject.values.lineInMute)
                barData[n].value = display.getTheme().grey_22;
        }
    }
    else
    {
        if (mtProject.values.trackMute[n])
            barData[n].value = display.getTheme().grey_22;
    }

    display.setControlShow(barControl[n]);
    display.setControlColors(barControl[n], levelBarColors[n]);
    display.setControlValue(barControl[n], level->value);
    display.refreshControl(barControl[n]);
}

void cMasterParams::showTrackVolumeBar(uint8_t n)
{
    float trackVolumeInDecybels = calcLevelDecybelValue(mtProject.values.trackVolume[n]);

    sprintf(mixerVolumeLabel[n], "%0.2f dB", trackVolumeInDecybels);

    display.setControlShow(trackVolumeBar[n]);

    if (mtProject.values.trackMute[n])
        display.setControlColors(trackVolumeBar[n], display.getTheme().inactiveBarColors);
    else
        display.setControlColors(trackVolumeBar[n], display.getTheme().activeBarColors);

    display.setControlValue(trackVolumeBar[n], mtProject.values.trackVolume[n]);

    display.setControlText(label[n], mixerVolumeLabel[n]);

    display.refreshControl(label[n]);
    display.refreshControl(trackVolumeBar[n]);

    display.synchronizeRefresh();
}

void cMasterParams::showDelayVolumeBar()
{
    float delayVolumeInDecybels = calcLevelDecybelValue(mtProject.values.delayVolume);

    sprintf(delayVolumeLabel, "%0.2f dB", delayVolumeInDecybels);

    display.setControlShow(trackVolumeBar[delayVolumeScreenPosition]);

    if (mtProject.values.delayMute)
        display.setControlColors(trackVolumeBar[delayVolumeScreenPosition],
                                 display.getTheme().inactiveBarColors);
    else
        display.setControlColors(trackVolumeBar[delayVolumeScreenPosition],
                                 display.getTheme().activeBarColors);

    display.setControlValue(trackVolumeBar[delayVolumeScreenPosition],
                            mtProject.values.delayVolume);

    display.setControlText(label[delayVolumeScreenPosition], delayVolumeLabel);

    display.refreshControl(label[delayVolumeScreenPosition]);
    display.refreshControl(trackVolumeBar[delayVolumeScreenPosition]);

    display.synchronizeRefresh();
}
void cMasterParams::showReverbVolumeBar()
{
    float reverbVolumeInDecybels = calcLevelDecybelValue(mtProject.values.reverbVolume);

    sprintf(reverbVolumeLabel, "%0.2f dB", reverbVolumeInDecybels);

    display.setControlShow(trackVolumeBar[reverbVolumeScreenPosition]);

    if (mtProject.values.reverbMute)
        display.setControlColors(trackVolumeBar[reverbVolumeScreenPosition],
                                 display.getTheme().inactiveBarColors);
    else
        display.setControlColors(trackVolumeBar[reverbVolumeScreenPosition],
                                 display.getTheme().activeBarColors);

    display.setControlValue(trackVolumeBar[reverbVolumeScreenPosition],
                            mtProject.values.reverbVolume);

    display.setControlText(label[reverbVolumeScreenPosition], reverbVolumeLabel);

    display.refreshControl(label[reverbVolumeScreenPosition]);
    display.refreshControl(trackVolumeBar[reverbVolumeScreenPosition]);

    display.synchronizeRefresh();
}

void cMasterParams::showDryMixVolumeBar()
{
    float dryMixVolumeInDecybels = calcLevelDecybelValue(mtProject.values.dryMixVolume);

    sprintf(dryMixVolumeLabel, "%0.2f dB", dryMixVolumeInDecybels);

    display.setControlShow(trackVolumeBar[dryMixVolumeScreenPosition]);

    if (mtProject.values.dryMixMute)
        display.setControlColors(trackVolumeBar[dryMixVolumeScreenPosition],
                                 display.getTheme().inactiveBarColors);
    else
        display.setControlColors(trackVolumeBar[dryMixVolumeScreenPosition],
                                 display.getTheme().activeBarColors);

    display.setControlValue(trackVolumeBar[dryMixVolumeScreenPosition],
                            mtProject.values.dryMixVolume);

    display.setControlText(label[dryMixVolumeScreenPosition], dryMixVolumeLabel);

    display.refreshControl(label[dryMixVolumeScreenPosition]);
    display.refreshControl(trackVolumeBar[dryMixVolumeScreenPosition]);

    display.synchronizeRefresh();
}

void cMasterParams::showLineInVolumeBar()
{
    float lineInVolumeInDecybels = calcLevelDecybelValue(mtProject.values.lineInVolume);

    sprintf(lineInVolumeLabel, "%0.2f dB", lineInVolumeInDecybels);

    display.setControlShow(trackVolumeBar[lineInVolumeScreenPosition]);

    if (mtProject.values.lineInMute)
        display.setControlColors(trackVolumeBar[lineInVolumeScreenPosition],
                                 display.getTheme().inactiveBarColors);
    else
        display.setControlColors(trackVolumeBar[lineInVolumeScreenPosition],
                                 display.getTheme().activeBarColors);

    display.setControlValue(trackVolumeBar[lineInVolumeScreenPosition],
                            mtProject.values.lineInVolume);

    display.setControlText(label[lineInVolumeScreenPosition], lineInVolumeLabel);

    display.refreshControl(label[lineInVolumeScreenPosition]);
    display.refreshControl(trackVolumeBar[lineInVolumeScreenPosition]);

    display.synchronizeRefresh();
}

#ifdef FEATURE_SYNTH
void cMasterParams::showSynthGain(int synthIdx)
{
    float value = mtProject.values.synthPart[synthIdx].gainDb;
    int position = synthVolumeScreenPositionStart + synthIdx;

    snprintf(synthVolumeLabel[synthIdx], sizeof(synthVolumeLabel[synthIdx]), "%0.02f dB", value);

    display.setControlText(label[position], synthVolumeLabel[synthIdx]);
    display.refreshControl(label[position]);

    display.setControlShow(trackVolumeBar[position]);
    display.setControlValue(trackVolumeBar[position],
                            map<int>(value, Synth::kGainSilence, Synth::kGainMax, 0, 100));
    display.refreshControl(trackVolumeBar[position]);
}
#endif

void cMasterParams::showLineInDelaySend()
{
    const uint8_t pos = 6;
    float valDb = calcLevelDecybelValue(mtProject.values.lineInDelaySend);

    if (buttons->isButtonPressed(interfaceButtonShift))
    {
        snprintf(lineInDelaySendLabel,
                 sizeof(lineInDelaySendLabel),
                 "%s",
                 mtProject.values.lineInDelaySendMute ? "Unmute" : "Mute");
    }
    else
    {
        snprintf(lineInDelaySendLabel, sizeof(lineInDelaySendLabel), "%0.2f dB", valDb);
    }

    display.setControlText2(label[pos], lineInDelaySendLabel);
    display.setControlColors(label[pos],
                             mtProject.values.lineInDelaySendMute
                               ? display.getTheme().inactiveLabelsColors
                               : display.getTheme().activeLabelsColors);

    display.setControlColors(barControl[pos],
                             mtProject.values.lineInDelaySendMute
                               ? display.getTheme().inactiveBarColors
                               : display.getTheme().activeBarColors);
    barData[pos].value =
      mtProject.values.lineInDelaySendMute ? display.getTheme().grey_22 : display.getTheme().white;

    display.setControlShow(barControl[pos]);
    display.setControlValue(barControl[pos], mtProject.values.lineInDelaySend);

    display.refreshControl(label[pos]);
    display.refreshControl(barControl[pos]);
    display.synchronizeRefresh();
}

void cMasterParams::showLineInReverbSend()
{
    const uint8_t pos = 4;
    float valDb = calcLevelDecybelValue(mtProject.values.lineInReverbSend);

    if (buttons->isButtonPressed(interfaceButtonShift))
    {
        snprintf(lineInReverbSendLabel,
                 sizeof(lineInReverbSendLabel),
                 "%s",
                 mtProject.values.lineInReverbSendMute ? "Unmute" : "Mute");
    }
    else
    {
        snprintf(lineInReverbSendLabel, sizeof(lineInReverbSendLabel), "%0.2f dB", valDb);
    }

    display.setControlText2(label[pos], lineInReverbSendLabel);
    display.setControlColors(label[pos],
                             mtProject.values.lineInReverbSendMute
                               ? display.getTheme().inactiveLabelsColors
                               : display.getTheme().activeLabelsColors);

    display.setControlColors(barControl[pos],
                             mtProject.values.lineInReverbSendMute
                               ? display.getTheme().inactiveBarColors
                               : display.getTheme().activeBarColors);
    barData[pos].value =
      mtProject.values.lineInReverbSendMute ? display.getTheme().grey_22 : display.getTheme().white;

    display.setControlShow(barControl[pos]);
    display.setControlValue(barControl[pos], mtProject.values.lineInReverbSend);

    display.refreshControl(label[pos]);
    display.refreshControl(barControl[pos]);

    display.synchronizeRefresh();
}

void cMasterParams::showEditTracksNamesMode()
{
    if (editTrackNameMode)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            display.setControlColors(label[i], display.getTheme().masterEditTrackNameLabelsColors);
            display.setControlText2(label[i], "Edit");
            display.refreshControl(label[i]);
        }

        display.synchronizeRefresh();
    }
    else
    {
        showMixerScreen();
    }
}

void cMasterParams::showKeyboardExport()
{

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(barControl[i]);

        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");

        display.setControlHide(trackVolumeBar[i]);
        display.setControlHide(barControl[i]);
        display.setControlHide(trackNameLabel[i]);

        display.setControlValue(label[i], 1);
    }

    display.setControlValue(bgLabel, 255);
    display.refreshControl(bgLabel);

    display.setControlText(label[0], "Enter");
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], "Save");

    display.setControlValue(label[0], 0);
    display.setControlValue(label[6], 0);
    display.setControlValue(label[7], 0);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.refreshControl(label[i]);
    }

    display.synchronizeRefresh();
}

void cMasterParams::refreshAllTracksMixerVolume()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        if (refreshVolumeFromCC[i])
        {
            showTrackVolumeBar(i);
            refreshVolumeFromCC[i] = false;
        }
    }

    display.synchronizeRefresh();
}
void cMasterParams::refreshOtherMixerVolume()
{
    for (uint8_t i = 8; i < 12; i++)
    {
        if (refreshVolumeFromCC[i])
        {
            switch (i)
            {
                case 8: showDelayVolumeBar(); break;
                case 9: showReverbVolumeBar(); break;
                case 10: showDryMixVolumeBar(); break;
                case 11:
                    showLineInVolumeBar();
                    break;
                    // sendy?
            }
            refreshVolumeFromCC[i] = false;
        }
    }
}

void cMasterParams::hideControlUnderMenu()
{
    if (displayType == displayMode::mixerDelayReverb)
    {
        for (int idx = 0; idx < 7; idx++)
        {
            display.setControlHide(trackNameLabel[idx]);
            display.setControlHide(trackVolumeBar[idx]);
            display.refreshControl(trackVolumeBar[idx]);
            display.refreshControl(trackNameLabel[idx]);
        }

        for (int barIdx = 0; barIdx < 8 + kSynthParts; barIdx++)
        {
            display.setControlHide(barControl[barIdx]);
            display.refreshControl(barControl[barIdx]);
        }

        display.setControlHide(blankSpaceCover[0]);
        display.setControlHide(blankSpaceCover[1]);
        display.setControlHide(blankSpaceCover[2]);

        display.setControlHide(blankSpaceCover[0]);
        display.setControlHide(blankSpaceCover[1]);
        display.setControlHide(blankSpaceCover[2]);
    }
}
