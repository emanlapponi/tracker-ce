#include "sampleRecorder.h"
#include <Recorder.h>

#include "Platform.h"
#include "core/interfacePopups.h"
#include <cmath>

static uint16_t framesPlacesS1[8][4] = { { 0 + 1, 31, 800 / 8 - 3, 391 },
                                         { (800 / 8) * 1 + 1, 31, 800 / 8 - 3, 391 },
                                         { (800 / 8) * 2 + 1, 31, 800 / 8 - 3, 391 },
                                         { (800 / 8) * 3 + 1, 31, 800 / 8 - 3, 391 },
                                         { (800 / 8) * 4 + 1, 31, 800 / 8 - 3, 391 },
                                         { (800 / 8) * 5 + 1, 31, 800 / 8 - 3, 391 },
                                         { (800 / 8) * 6 + 1, 31, 800 / 8 - 3, 391 },
                                         { (800 / 8) * 7 + 1, 31, 800 / 8 - 3, 391 } };

static uint16_t framesPlacesS2[8][4] = {
    { 0 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 1 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 2 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 3 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 4 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 5 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 6 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 7 + 1, 424, 800 / 8 - 3, 55 },
};

void cSampleRecorder::initDisplayControls()
{
    for (uint8_t i = 0; i < 5; i++)
    {
        levelBarColors[i] = display.getTheme().activeBarColors[i];
    }

    points.pointsType = 0;
    points.endPoint = MAX_16BIT;
    points.startPoint = 0;
    for (int ch = 0; ch < SAMPLE_CHANNELS; ch++)
    {
        spectrum[ch].init();
    }

    strControlProperties prop;
    prop.x = 100;
    prop.y = 75 + ((300 - (MAX_16BIT / 300)) / 2);
    prop.w = 600;
    prop.h = MAX_16BIT / 300;
    playhead = display.createControl<cProgressCursor>(&prop);
    prop.data = &points;
    pointsControl = display.createControl<cPoints>(&prop);
    prop.data = &spectrum[0];
    spectrumControl[0] = display.createControl<cSpectrum>(&prop);
    if (SAMPLE_CHANNELS == 2)
    {
        prop.h = (MAX_16BIT / 300) / 2;
        prop.y = 75 + ((300 - (MAX_16BIT / 300)) / 2) + (MAX_16BIT / 300) / 2;
        prop.data = &spectrum[1];
        spectrumControl[1] = display.createControl<cSpectrum>(&prop);
    }

    doubleArrow[0].bitmaps[0].bitmapIndex = displayDoubleArrowL;
    doubleArrow[0].bitmaps[0].xValue = (800 / 8) * RADIO_COL2 + (800 / 16);
    doubleArrow[0].bitmaps[0].yValue = 445;
    doubleArrow[1].bitmaps[0].bitmapIndex = displayDoubleArrowR;
    doubleArrow[1].bitmaps[0].xValue = (800 / 8) * RADIO_COL3 + (800 / 16);
    doubleArrow[1].bitmaps[0].yValue = 445;

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

    strControlProperties prop3;
    // ramka
    frameData.placesCount = 8;
    frameData.startPlace = 0;
    frameData.places[0] = &framesPlacesS1[0][0];
    frameData.places[1] = &framesPlacesS1[1][0];
    frameData.places[2] = &framesPlacesS1[2][0];
    frameData.places[3] = &framesPlacesS1[3][0];
    frameData.places[4] = &framesPlacesS1[4][0];
    frameData.places[5] = &framesPlacesS1[5][0];
    frameData.places[6] = &framesPlacesS1[6][0];
    frameData.places[7] = &framesPlacesS1[7][0];
    prop3.style = 0;
    prop3.value = 0;
    prop3.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop3);

    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.value = 255;
    prop2.style = controlStyleNoTransparency | controlStyleShow;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    prop2.value = 0;
    prop2.data = nullptr;
    prop2.text = nullptr;
    prop2.colors = display.getTheme().blankSpaceCoverLabelColors;
    prop2.style = controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop2.x = (800 / 8) * 7 + 2;
    prop2.w = 800 / 8 - 3;
    prop2.y = 29;
    prop2.h = 394;
    blankSpaceCover = display.createControl<cLabel>(&prop2);

    prop2.x = (800 / 8) * RADIO_COL2 + 2;
    prop2.w = (800 / 8) * 2 - 3;
    radioSpaceCover = display.createControl<cLabel>(&prop2);

    strControlProperties prop4;
    sourceList.linesCount = sourceCount;
    sourceList.start = recorderConfig.source;
    sourceList.length = sourceCount;
    sourceList.data = (char**)ptrSourcesNamesLabels;
    prop4.x = 0 + 2;
    prop4.y = 29;
    prop4.w = 800 / 8 - 3;
    prop4.h = 394;
    prop4.data = &sourceList;
    prop4.style = controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    sourceListControl = display.createControl<cList>(&prop4);

    monitorList.linesCount = 2;
    monitorList.start = recorderConfig.monitor;
    monitorList.length = 2;
    monitorList.data = monitorNames;
    prop4.x = (800 / 8) * (MONITOR_COL) + 2;
    prop4.y = 29;
    prop4.w = 800 / 8 - 3;
    prop4.h = 394;
    prop4.data = &monitorList;
    monitorListControl = display.createControl<cList>(&prop4);

    prop4.value = 0;
    prop4.data = nullptr;
    prop4.text = nullptr;
    prop4.colors = display.getTheme().blankSpaceCoverLabelColors;
    prop4.style = controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop4.x = (800 / 8) * (MONITOR_COL) + 2;
    prop4.y = 29;
    prop4.w = 800 / 8;
    prop4.h = 394;
    monitorListSpaceCover = display.createControl<cLabel>(&prop4);

    strControlProperties prop5;
    prop5.x = (800 / 8) * (LEVEL_COL) + 2;
    prop5.y = 29;
    prop5.w = 800 / 8 - 3;
    prop5.style = controlStyleShow | controlStyleValue_0_100 | controlStyleBackground |
                  (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop5.h = 394;
    prop5.colors = levelBarColors;
    levelBarControl = display.createControl<cBar>(&prop5);

    prop5.x = (800 / 8) * (GAIN_COL) + 2;
    prop5.colors = nullptr;
    gainBarControl = display.createControl<cBar>(&prop5);

    prop5.x = (800 / 8) * (RADIO_COL1) + 2;
    radioFreqBarControl = display.createControl<cBar>(&prop5);

    strControlProperties prop10;
    prop10.text = (char*)"";
    prop10.style = (controlStyleCenterX | controlStyleCenterY | controlStyleBackground);
    prop10.x = (800 / 8) * RADIO_COL3 - 1;
    prop10.w = 800 / 4 - 3;
    prop10.h = 30;
    prop10.y = 200;
    prop10.colors = display.getTheme().blankSpaceCoverLabelColors;
    radioRdsLabel = display.createControl<cLabel>(&prop10);
}

void cSampleRecorder::showDefaultScreen()
{
    titleBar->show();
    titleBar->setTitle("Sample Recorder");

    for (int i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlValue(label[i], 1);
    }

    display.setControlData(label[RADIO_COL2], &doubleArrow[0]);
    display.setControlData(label[RADIO_COL3], &doubleArrow[1]);
    display.setRemoveControlStyle(label[RADIO_COL2], controlStyleShowBitmap);
    display.setRemoveControlStyle(label[RADIO_COL3], controlStyleShowBitmap);

    // odciemnanie labela wrazie deaktywacji save po wyjeciu karty
    display.setControlColors(label[6], display.getTheme().activeLabelsColors);
    display.setControlColors(label[7], display.getTheme().activeLabelsColors);
    display.setControlStyle(label[6],
                            controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
    display.setControlStyle(label[7],
                            controlStyleCenterX | controlStyleCenterY | controlStyleFont3);

    if (currentScreen == screenTypeConfig)
    {
        showGain();
        showSource();
        showMonitor();

        // spectrum
        for (int channel = 0; channel < SAMPLE_CHANNELS; channel++)
        {
            display.setControlHide(spectrumControl[channel]);
            display.refreshControl(spectrumControl[channel]);
        }

        // cursor
        display.setControlHide(playhead);
        display.refreshControl(playhead);

        // points
        display.setControlHide(pointsControl);
        display.refreshControl(pointsControl);

        // lists
        display.setControlShow(sourceListControl);
        display.setControlValue(sourceListControl, recorderConfig.source);
        display.refreshControl(sourceListControl);

        showMonitorList();

        // bar
        display.setControlValue(levelBarControl, levelBarVal);
        display.setControlShow(levelBarControl);
        display.refreshControl(levelBarControl);

        display.setControlValue(gainBarControl, gainBarVal);
        display.setControlShow(gainBarControl);
        display.refreshControl(gainBarControl);

        if (recorderConfig.source == sourceTypeRadio)
        {
            display.setControlValue(bgLabel, 255); // 243

            showRadio();
            calcRadioFreqBarVal();
            drawRadioFreqBar();
            showFreqValue();
        }
        else
        {
            hideRadio();

            if (selectedPlace == RADIO_COL1 || selectedPlace == RADIO_COL2 ||
                selectedPlace == RADIO_COL3)
            {
                selectedPlace = 0;
            }
        }

        // bottom labels
        display.setControlValue(label[LEVEL_COL], 0);
        display.setControlValue(label[7], 0);
        display.setControlText(label[0], "Source");
        display.setControlText(label[LEVEL_COL], "Level");
        display.setControlText(label[GAIN_COL], "Gain");

        display.setControlText(label[7], "Record");
        display.setAddControlStyle(label[7], controlStyleBackground);
        display.setControlColors(label[7], display.getTheme().redButtonLabelsColors);

        frameData.placesCount = 8;
        frameData.startPlace = 0;
        frameData.places[0] = &framesPlacesS1[0][0];
        frameData.places[1] = &framesPlacesS1[1][0];
        frameData.places[2] = &framesPlacesS1[2][0];
        frameData.places[3] = &framesPlacesS1[3][0];
        frameData.places[4] = &framesPlacesS1[4][0];
        frameData.places[5] = &framesPlacesS1[5][0];
        frameData.places[6] = &framesPlacesS1[6][0];
        frameData.places[7] = &framesPlacesS1[7][0];

        display.setControlData(frameControl, &frameData);
        display.setControlShow(frameControl);
        display.refreshControl(frameControl);
        display.refreshControl(bgLabel);

        calcLevelBarVal();
        drawLevelBar();

        calcGainBarVal();
        drawGainBar();

        keyboardManager.deactivateKeyboard();

        display.setControlSize(blankSpaceCover, 800 / 8 - 3, -1);
        display.setControlPosition(blankSpaceCover, (800 / 8) * 7 + 2, -1);
        display.setControlShow(blankSpaceCover);
        display.refreshControl(blankSpaceCover);

        clearAllNodes();
        cancelMultiFrame();
    }
    else if (currentScreen == screenTypeRecord)
    {
        display.setControlHide(radioRdsLabel);
        display.refreshControl(radioRdsLabel);

        resizeSpectrum();

        for (int channel = 0; channel < SAMPLE_CHANNELS; channel++)
        {
            if (isChannelShown(channel))
            {
                display.setControlShow(spectrumControl[channel]);
            }
            else
            {
                display.setControlHide(spectrumControl[channel]);
            }

            display.refreshControl(spectrumControl[channel]);
        }

        display.setControlShow(playhead);
        display.refreshControl(playhead);

        if (recordInProgressFlag == 1)
        {
            display.setControlHide(pointsControl);
            display.refreshControl(pointsControl);
        }
        else
        {
            display.setControlShow(pointsControl);
            display.refreshControl(pointsControl);
        }

        display.setControlHide(radioSpaceCover);
        display.setControlHide(blankSpaceCover);
        display.setControlHide(radioRdsLabel);
        display.setControlHide(sourceListControl);
        display.setControlHide(monitorListControl);
        display.setControlHide(monitorListSpaceCover);
        display.setControlHide(levelBarControl);
        display.setControlHide(gainBarControl);
        display.setControlHide(radioFreqBarControl);
        display.refreshControl(blankSpaceCover);

        frameData.placesCount = 8;
        frameData.startPlace = 0;
        frameData.places[0] = &framesPlacesS2[0][0];
        frameData.places[1] = &framesPlacesS2[1][0];
        frameData.places[2] = &framesPlacesS2[2][0];
        frameData.places[3] = &framesPlacesS2[3][0];
        frameData.places[4] = &framesPlacesS2[4][0];
        frameData.places[5] = &framesPlacesS2[5][0];
        frameData.places[6] = &framesPlacesS2[6][0];
        frameData.places[7] = &framesPlacesS2[7][0];

        display.setControlData(frameControl, &frameData);

        if (recordInProgressFlag == 1)
        {
            for (int i = 0; i < 7; i++)
            {
                display.setControlText(label[i], "");
                display.setControlText2(label[i], "");
                display.setControlColors(label[METRONOME_COL],
                                         i < 4 ? display.getTheme().activeButtonLabelsColors
                                               : display.getTheme().activeLabelsColors);
            }
            display.setControlValue(bgLabel, 255); // 140
            display.setControlText(label[7], "Stop");

            display.setControlHide(frameControl);
        }
        else
        {
            display.setControlText(label[0], "Preview");
            display.setControlText2(label[0], "-");
            display.setControlText(label[1], "Start Point");
            display.setControlText(label[2], "End Point");
            display.setControlText(label[3], "Zoom");
            display.setControlText(label[4], "Crop");
            display.setControlText(label[5], "Undo");
            display.setControlText(label[6], "Cancel");
            display.setControlText(label[7], "Save");
            display.setControlValue(bgLabel, 255);

            if (!fileSystem->isSdCardInserted())
            {
                // dezaktywacja kiedy brak karty sd
                display.setControlColors(label[7], display.getTheme().inactiveLabelsColors);
            }

            display.setControlShow(frameControl);
            display.refreshControl(frameControl);
        }

        display.setControlValue(label[4], 0);
        display.setControlValue(label[5], 0);
        display.setControlValue(label[6], 0);
        display.setControlValue(label[7], 0);

        keyboardManager.deactivateKeyboard();

        if (selectedPlace == 1)
        {
            points.selected = selectStart;
        }
        else if (selectedPlace == 2)
        {
            points.selected = selectEnd;
        }

        clearAllNodes();
        cancelMultiFrame();
    }
    else if (currentScreen == screenTypeKeyboard)
    {
        display.setControlHide(blankSpaceCover);
        display.setControlHide(radioRdsLabel);

        // spectrum
        for (int channel = 0; channel < SAMPLE_CHANNELS; channel++)
        {
            display.setControlHide(spectrumControl[channel]);
            display.refreshControl(spectrumControl[channel]);
        }

        // cursor
        display.setControlHide(playhead);
        display.refreshControl(playhead);

        display.setControlHide(frameControl);
        display.setControlHide(pointsControl);
        display.setControlHide(radioSpaceCover);

        keyboardManager.activateKeyboard(32);

        display.setControlText(label[0], "Enter");
        display.setControlText2(label[0], "");
        display.setControlText(label[1], "Auto Name");
        display.setControlText2(label[1], "");
        display.setControlText(label[2], "");
        display.setControlText2(label[2], "");
        display.setControlText(label[3], "");
        display.setControlText2(label[3], "");
        display.setControlText(label[4], "");
        display.setControlText2(label[4], "");
        display.setControlText(label[5], "Cancel");
        display.setControlText(label[6], "Save & Load");
        display.setControlText(label[7], "Save");

        for (uint8_t i = 0; i < 8; i++)
        {
            display.setControlValue(label[i], 0);
        }
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }

    if ((currentScreen == screenTypeRecord) && !recordInProgressFlag)
    {
        showZoomValue();
        showEndPointValue();
        showStartPointValue();
    }

    mtPopups.hideInfoPopup();
    display.synchronizeRefresh();
}

void cSampleRecorder::showRadio()
{
    calcRadioFreqBarVal();

    display.setControlText(label[RADIO_COL1], "Radio Freq");
    display.setControlText2(label[RADIO_COL1], "");
    display.setAddControlStyle(label[RADIO_COL2], controlStyleShowBitmap);
    display.setAddControlStyle(label[RADIO_COL3], controlStyleShowBitmap);
    display.setControlText(label[RADIO_COL2], "");
    display.setControlText(label[RADIO_COL3], "");
    display.setControlText2(label[RADIO_COL2], "");
    display.setControlText2(label[RADIO_COL3], "");

    display.refreshControl(label[RADIO_COL2]);
    display.refreshControl(label[RADIO_COL3]);

    display.setControlValue(radioFreqBarControl, radioFreqBarVal);
    display.setControlShow(radioFreqBarControl);
    display.refreshControl(radioFreqBarControl);

    display.setControlPosition(radioSpaceCover, (800 / 8) * RADIO_COL2 + 2, -1);
    display.setControlSize(radioSpaceCover, (800 / 8) * 2 - 3, -1);
    display.setControlShow(radioSpaceCover);
    display.refreshControl(radioSpaceCover);

    display.setControlShow(radioRdsLabel);
    display.setControlText(radioRdsLabel, "");
    display.refreshControl(radioRdsLabel);

    display.setControlValue(bgLabel, 255); // 0b11111111
    display.refreshControl(bgLabel);

    showFreqValue();
}

void cSampleRecorder::hideRadio()
{
    display.setControlValue(radioFreqBarControl, radioFreqBarVal);
    display.setControlHide(radioFreqBarControl);

    display.setControlText(label[RADIO_COL1], "");
    display.setControlText(label[RADIO_COL2], "");
    display.setControlText(label[RADIO_COL3], "");
#ifdef PLATFORM_IMX
    display.setControlText(label[METRONOME_COL], "Metronome");
#else
    display.setControlText(label[METRONOME_COL], "Metronome");
#endif

    display.setControlText2(label[RADIO_COL1], "");
    display.setControlText2(label[RADIO_COL2], "");
    display.setControlText2(label[RADIO_COL3], "");
#ifdef PLATFORM_IMX
    display.setControlText2(label[METRONOME_COL], "Config");
#else
    display.setControlText2(label[METRONOME_COL], "Config");
#endif

    display.setControlValue(label[METRONOME_COL], 1);

    display.setControlStyle2(label[METRONOME_COL],
                             controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
    display.setControlColors(label[METRONOME_COL], display.getTheme().activeButtonLabelsColors);
    display.setRemoveControlStyle(label[RADIO_COL2], controlStyleShowBitmap);
    display.setRemoveControlStyle(label[RADIO_COL3], controlStyleShowBitmap);

    display.refreshControl(label[RADIO_COL1]);
    display.refreshControl(label[RADIO_COL2]);
    display.refreshControl(label[RADIO_COL3]);
    display.refreshControl(label[METRONOME_COL]);

    display.setControlPosition(
      radioSpaceCover, (800 / 8) * (RADIO_COL1 < RADIO_COL3 ? RADIO_COL1 : RADIO_COL3) + 2, -1);
    display.setControlSize(radioSpaceCover, (800 / 8) * 3 - 3, -1);
    display.setControlShow(radioSpaceCover);
    display.refreshControl(radioSpaceCover);

    display.setControlHide(radioRdsLabel);

    display.setControlValue(bgLabel, 255); // 0b11111011
    display.refreshControl(bgLabel);
}

void cSampleRecorder::hideRDS()
{
    display.setControlHide(radioRdsLabel);
    display.refreshControl(radioRdsLabel);
}

//==============================================================================================================
void cSampleRecorder::activateLabelsBorder()
{
    if (selectedPlace > frameData.placesCount - 1)
    {
        return;
    }

    display.setControlValue(frameControl, selectedPlace);
    display.setControlShow(frameControl);
    display.refreshControl(frameControl);
}

//==============================================================================================================
void cSampleRecorder::showZoomValue()
{
    snprintf(zoomTextValue, sizeof(zoomTextValue), "%.2f", zoom.zoomValue);

    display.setControlText2(label[3], zoomTextValue);
    display.refreshControl(label[3]);
}

void cSampleRecorder::showRecTimeValue()
{
    recTimeValue = recorder.getLength() / 44100.0;

    formatTimeSeconds(recTimeValueText, sizeof(recTimeValueText), recTimeValue);

    display.setControlText2(label[2], recTimeValueText);
    display.refreshControl(label[2]);
}

void cSampleRecorder::showPreviewValue()
{
    float playTimeValue =
      ((playProgressValue * ((float)recorder.getLength() / MAX_16BIT)) / 44100.0);
    float localEndPoint = (recorder.getLength() * endPoint) / MAX_16BIT;

    if (playTimeValue >= (localEndPoint - 0.01))
        playTimeValue = localEndPoint;

    formatTimeSeconds(playTimeValueText, sizeof(playTimeValueText), playTimeValue);

    display.setControlText(label[0], "Preview");
    display.setControlText2(label[0], playTimeValueText);
    display.setControlColors(label[0], display.getTheme().activeLabelsColors);
    display.setControlShow(label[0]);
    display.refreshControl(label[0]);
}
void cSampleRecorder::hidePreviewValue()
{
    display.setControlText(label[0], "Preview");
    display.setControlText2(label[0], "-");
    display.setControlColors(label[0], display.getTheme().activeLabelsColors);
    display.refreshControl(label[0]);
}
void cSampleRecorder::showStartPointValue()
{
    recTimeValue = recorder.getLength() / 44100.0;
    float localStartPoint = (recTimeValue * startPoint) / MAX_16BIT;

    formatTimeSeconds(startPointValueText, sizeof(startPointValueText), localStartPoint);

    display.setControlText2(label[1], startPointValueText);
    display.refreshControl(label[1]);
}

void cSampleRecorder::showEndPointValue()
{
    recTimeValue = recorder.getLength() / 44100.0;
    float localEndPoint = (recTimeValue * endPoint) / MAX_16BIT;

    formatTimeSeconds(endPointValueText, sizeof(endPointValueText), localEndPoint);

    display.setControlText2(label[2], endPointValueText);
    display.refreshControl(label[2]);
}

void cSampleRecorder::showFreqValue()
{
    snprintf(freqTextValue, 9, "%.1fMHz", recorderConfig.radioFreq);

    display.setControlText2(label[RADIO_COL1], freqTextValue);
    display.refreshControl(label[RADIO_COL1]);
}

void cSampleRecorder::showSourceList()
{
    sourceList.start = recorderConfig.source;
    sourceList.length = sourceCount;
    sourceList.linesCount = sourceCount;
    sourceList.data = (char**)ptrSourcesNamesLabels;

    display.setControlData(sourceListControl, &sourceList);
    display.setControlShow(sourceListControl);
    display.refreshControl(sourceListControl);
}

void cSampleRecorder::showMonitorList()
{
    monitorList.start = recorderConfig.monitor;
    monitorList.length = monitorCount;
    monitorList.linesCount = 2;
    monitorList.data = monitorNames;

    display.setControlData(monitorListControl, &monitorList);
    if (isMonitorListVisible())
    {
        display.setControlShow(monitorListControl);
        display.setControlHide(monitorListSpaceCover);
    }
    else
    {
        display.setControlHide(monitorListControl);
        display.setControlShow(monitorListSpaceCover);
    }
    display.refreshControl(monitorListControl);
    display.refreshControl(monitorListSpaceCover);
}

void cSampleRecorder::drawRadioFreqBar()
{
    display.setControlValue(radioFreqBarControl, radioFreqBarVal);
    display.setControlShow(radioFreqBarControl);
    display.refreshControl(radioFreqBarControl);
    showFreqValue();
}

void cSampleRecorder::drawLevelBar()
{
    if (currentScreen != screenTypeConfig)
        return;

    uint32_t one_true_red = display.getTheme().red;
    uint32_t one_true_green = display.getTheme().green;

    if (levelBarVal < 85)
    {
        if (redColorTimer < 350)
        {
            levelBarColors[0] = one_true_red;
        }
        else
        {
            if (levelBarVal > 70)
            {
                uint8_t green =
                  mapClamped<int>(levelBarVal, 70, 85, ((one_true_green & 0xff00) >> 8), 0);
                levelBarColors[0] = (one_true_red & 0xff0000) | (green << 8);
            }
            else if (levelBarVal > 60)
            {
                // uint8_t green = map(levelBarVal,40,60,((one_true_green&0xff00)>>8),0);
                uint8_t red =
                  mapClamped<int>(levelBarVal, 60, 70, 0, ((one_true_red & 0xff0000) >> 16));
                levelBarColors[0] = (red << 16) | (one_true_green & 0xff00);
            }
            else
            {
                levelBarColors[0] = one_true_green;
            }
        }

        if (redColorTimer > 3000000)
            redColorTimer = 201;
    }
    else
    {
        redColorTimer = 0;
        levelBarColors[0] = one_true_red;
    }

    display.setControlValue(levelBarControl, levelBarVal);
    display.setControlShow(levelBarControl);
    display.refreshControl(levelBarControl);
}
void cSampleRecorder::drawGainBar()
{
    display.setControlValue(gainBarControl, gainBarVal);
    display.setControlShow(gainBarControl);
    display.refreshControl(gainBarControl);
}

void cSampleRecorder::showSelectionWindow()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");

        display.refreshControl(label[i]);
    }

    display.setControlValue(label[6], 0);
    display.setControlValue(label[7], 0);
    display.setControlText(label[7], "Yes");
    display.setControlText(label[6], "Cancel");
    display.setControlColors(label[6], display.getTheme().highlightedButtonLabelsColors);
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    display.setAddControlStyle(label[6], controlStyleBackground);
    display.setAddControlStyle(label[7], controlStyleBackground);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    mtPopups.showInfoPopup("Changes will be lost.", "Do you want to continue?");
    // display.setControlText(popupWindowLabel,"Changes will be lost. Do you want to continue?");
    // display.setControlShow(popupWindowLabel);
    // display.refreshControl(popupWindowLabel);

    display.synchronizeRefresh();
}

void cSampleRecorder::showSelectionWindowSave()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");

        display.refreshControl(label[i]);
    }

    display.setControlValue(label[6], 0);
    display.setControlValue(label[7], 0);
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], "Yes");
    display.setControlColors(label[6], display.getTheme().highlightedButtonLabelsColors);
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    display.setAddControlStyle(label[6], controlStyleBackground);
    display.setAddControlStyle(label[7], controlStyleBackground);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    mtPopups.showInfoPopup("This name already exists.", "Do you want to overwrite it?");
    // display.setControlText(popupWindowLabel,"This name already exists. Do you want to overwrite
    // it?"); display.setControlShow(popupWindowLabel); display.refreshControl(popupWindowLabel);

    display.synchronizeRefresh();
}

void cSampleRecorder::showSelectionWindowFullMemory()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");

        display.refreshControl(label[i]);
    }

    display.setControlValue(label[7], 0);
    display.setControlText(label[7], "OK");
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    display.setAddControlStyle(label[7], controlStyleBackground);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    mtPopups.showInfoPopup("The memory is full.", "Recording has been stopped.");
    // display.setControlText(popupWindowLabel,"The memory is full. Recording has been stopped.");
    // display.setControlShow(popupWindowLabel);
    // display.refreshControl(popupWindowLabel);

    display.synchronizeRefresh();
}

void cSampleRecorder::showSelectionNotEnoughInstruments()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");

        display.refreshControl(label[i]);
    }

    display.setControlValue(label[7], 0);
    display.setControlText(label[7], "OK");
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    display.setAddControlStyle(label[7], controlStyleBackground);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    // display.setControlText(popupWindowLabel,"There are not enough instruments");

    mtPopups.showInfoPopup("There are not enough instruments", nullptr);
    // display.setControlShow(popupWindowLabel);
    // display.refreshControl(popupWindowLabel);

    display.synchronizeRefresh();
}

void cSampleRecorder::showSelectionNotEnoughMemory()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");

        display.refreshControl(label[i]);
    }

    display.setControlValue(label[7], 0);
    display.setControlText(label[7], "OK");
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    display.setAddControlStyle(label[7], controlStyleBackground);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    mtPopups.showInfoPopup("There is not enough Memory in sample bank.", nullptr);
    // display.setControlText(popupWindowLabel,"There is not enough Memory in sample bank.");
    // display.setControlShow(popupWindowLabel);
    // display.refreshControl(popupWindowLabel);

    display.synchronizeRefresh();
}

void cSampleRecorder::showSelectionStopPattern()
{
    titleBar->show();
    titleBar->setTitle("Sample Recorder");

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");

        display.refreshControl(label[i]);
        display.setControlShow(label[i]);
    }

    display.setControlValue(label[6], 0);
    display.setControlValue(label[7], 0);
    display.setControlText(label[7], "Yes");
    display.setControlText(label[6], "Cancel");
    display.setControlColors(label[6], display.getTheme().highlightedButtonLabelsColors);
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    display.setAddControlStyle(label[6], controlStyleBackground);
    display.setAddControlStyle(label[7], controlStyleBackground);

    display.refreshControl(label[6]);
    display.refreshControl(label[7]);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    mtPopups.showInfoPopup("This action will stop the pattern.", "Do you want to continue?");
    // display.setControlText(popupWindowLabel,"This action will stop the pattern. Do you want to
    // continue?"); display.setControlShow(popupWindowLabel);
    // display.refreshControl(popupWindowLabel);

    display.setControlValue(bgLabel, 255);
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);

    display.synchronizeRefresh();
}

void cSampleRecorder::refreshRDS()
{
    display.setControlText(radioRdsLabel, radio->getStationName());
    display.setControlShow(radioRdsLabel);
    display.refreshControl(radioRdsLabel);
}
void cSampleRecorder::displaySeeking()
{
    display.setControlText(radioRdsLabel, "Seeking...");
    display.setControlShow(radioRdsLabel);
    display.refreshControl(radioRdsLabel);
}
void cSampleRecorder::displayEmptyRDS()
{
    display.setControlText(radioRdsLabel, "");
    display.setControlShow(radioRdsLabel);
    display.refreshControl(radioRdsLabel);
}

void cSampleRecorder::showSource()
{
    if (currentScreen == screenTypeConfig)
    {
        display.setControlText2(label[0], ptrSourcesNamesLabels[recorderConfig.source]);
        display.setControlShow(label[0]);
        display.refreshControl(label[0]);
    }
}

void cSampleRecorder::showMonitor()
{
    if (currentScreen == screenTypeConfig)
    {
        display.setControlText(label[MONITOR_COL], isMonitorListVisible() ? "Monitor" : "");

        strcpy(monitorVal, monitorNamesLabels[recorderConfig.monitor]);
        display.setControlText2(label[MONITOR_COL], isMonitorListVisible() ? monitorVal : "");
        display.setControlShow(label[MONITOR_COL]);
        display.refreshControl(label[MONITOR_COL]);
    }
}

void cSampleRecorder::showGain()
{
    if (isLineInSource())
    {
        sprintf(gainVal, "%d", recorderConfig.gainLineIn);
    }
    else if (recorderConfig.source == sourceTypeRadio)
    {
        sprintf(gainVal, "%d", recorderConfig.gainRadio);
    }
    else if (recorderConfig.source == sourceTypeMicLG)
    {
        sprintf(gainVal, "%d", recorderConfig.gainMicLow);
    }
    else if (recorderConfig.source == sourceTypeMicHG)
    {
        sprintf(gainVal, "%d", recorderConfig.gainMicHigh);
    }
    else if (isUsbSource())
    {
        sprintf(gainVal, "%d", recorderConfig.gainUsb);
    }
    display.setControlText2(label[GAIN_COL], gainVal);
    display.setControlShow(label[GAIN_COL]);
    display.refreshControl(label[GAIN_COL]);
}

void cSampleRecorder::resizeSpectrum()
{
    bool isStereo = isStereoSource();

    if (isStereo)
    {
        display.setControlSize(spectrumControl[0], 600, (MAX_16BIT / 300) / 2);
        display.refreshControl(spectrumControl[0]);
    }
    else
    {
        display.setControlSize(spectrumControl[0], 600, (MAX_16BIT / 300));
        display.refreshControl(spectrumControl[0]);
    }
}

bool cSampleRecorder::isChannelShown(int channel)
{
    if (channel == 0)
    {
        return true;
    }
    else if (channel == 1)
    {
        return isStereoSource();
    }

    assert(false);
    return false;
}

void cSampleRecorder::formatTimeSeconds(char* destBuffer, size_t destBufferSize, float value)
{
    int valueCharacters = destBufferSize - 2; // 's' and '/0'

    char* valueBuffer = new char[valueCharacters];

    snprintf(
      valueBuffer, valueCharacters, "%.3f", value); // keep constant number of characters for value

    snprintf(destBuffer, destBufferSize, "%ss", valueBuffer);

    delete[] valueBuffer;
}

void cSampleRecorder::setCurrentScreenType(screenType screenType)
{
    currentScreen = screenType;
    refreshOutputSource();
}

void cSampleRecorder::refreshOutputSource()
{
    if (isUsbSource())
    {
        if (currentScreen == screenType::screenTypeConfig || recordInProgressFlag == 1)
        {
            engine.setOutputSource(audioEngine::OutputSourceType::kUsbInput);
        }
        else if (currentScreen == screenType::screenTypeKeyboard ||
                 currentScreen == screenType::screenTypeRecord)
        {
            engine.setOutputSource(audioEngine::OutputSourceType::kRegularMix);
        }
    }
    else
    {
        engine.setOutputSource(audioEngine::OutputSourceType::kRegularMix);
    }
}
