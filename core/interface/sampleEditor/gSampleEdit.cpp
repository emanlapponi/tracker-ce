#include "core/interfacePopups.h"
#include "fileManager.h"
#include "mtSampleEditorEngine.h"
#include "ptfx_manager.h"
#include "sampleEditor.h"

#include <cmath>
#include <string.h>

void cSampleEditor::initDisplayControls()
{
    // popups
    strControlProperties popupProgressProperties;
    popupProgressProperties.x = 190;
    popupProgressProperties.y = 170;
    popupProgressProperties.style = controlStyleValue_0_100 | controlStyleBackground;
    popupProgressProperties.h = 100;
    popupProgressProperties.w = 420;
    popupProgressBar = display.createControl<cHorizontalBar>(&popupProgressProperties);

    // frame
    strControlProperties frameProperties;
    frameProperties.style = 0;
    frameProperties.value = 0;
    frameProperties.data = &frameData;
    frame = display.createControl<cFrame>(&frameProperties);

    labelArrow.bitmaps[0].bitmapIndex = displayArrowU;
    labelArrow.bitmaps[0].xValue = (800 / 8) * 6 + (800 / 16);
    labelArrow.bitmaps[0].yValue = 450;
    labelArrow.bitmaps[1].bitmapIndex = displayArrowD;
    labelArrow.bitmaps[1].xValue = (800 / 8) * 7 + (800 / 16);
    labelArrow.bitmaps[1].yValue = 450;

    labelDataMore.dotIndicator.count = 2;
    labelDataMore.dotIndicator.current = 0;
    // label
    strControlProperties labelProperties;
    for (uint8_t i = 0; i < 8; i++)
    {
        labelProperties.value = 1;
        labelProperties.colors = display.getTheme().activeButtonLabelsColors;
        labelProperties.style = (controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        labelProperties.x = (800 / 8) * i + (800 / 16);
        labelProperties.w = 800 / 8 - 6;
        labelProperties.y = 424;
        labelProperties.h = 55;

        label[i] = display.createControl<cLabel>(&labelProperties);
    }

    labelProperties.value = 0;
    labelProperties.data = nullptr;
    labelProperties.text = nullptr;
    labelProperties.colors = display.getTheme().blankSpaceCoverLabelColors;
    labelProperties.style =
      controlStyleBackground |
      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    labelProperties.x = (800 / 8) * 7 + 1;
    labelProperties.w = 800 / 8 - 2;
    labelProperties.y = 29;
    labelProperties.h = 394;
    blankSpaceCover = display.createControl<cLabel>(&labelProperties);

    // bar
    strControlProperties barProperties;
    for (uint8_t i = 0; i < 8; i++)
    {
        barProperties.x = (800 / 8) * i + 1;
        barProperties.y = 29;
        barProperties.w = 800 / 8 - 4;
        barProperties.style =
          controlStyleValue_0_100 | controlStyleBackground | controlStyleFont2 |
          (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
          (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
        barProperties.h = 394;

        bar[i] = display.createControl<cBar>(&barProperties);
    }

    // parameter string lists (for PARAM_LIST columns 0..3)
    strControlProperties paramListProperties;
    for (uint8_t i = 0; i < kEffectPageParams; i++)
    {
        paramListData[i].linesCount = 1;
        paramListData[i].start = 0;
        paramListData[i].length = 1;
        paramListData[i].data = nullptr;

        paramListProperties.x = (800 / 8) * i + 1;
        paramListProperties.y = 29;
        paramListProperties.w = 800 / 8 - 4;
        paramListProperties.h = 394;
        paramListProperties.style =
          controlStyleBackground |
          (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
        paramListProperties.data = &paramListData[i];
        paramListProperties.colors = nullptr;
        paramList[i] = display.createControl<cList>(&paramListProperties);
    }

    // list
    PtfxManager::initEffectList();
    effectListData.linesCount = 14;
    effectListData.start = 0;
    effectListData.length = PtfxManager::getTotalEffectListCount();
    effectListData.data = PtfxManager::getEffectListNames();

    strControlProperties effectListProperties;
    effectListProperties.x = (800 / 8) * 6 + 1;
    effectListProperties.y = 29;
    effectListProperties.w = 800 / 4 - 3;
    effectListProperties.h = 394;
    effectListProperties.style =
      controlStyleBackground |
      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    effectListProperties.data = &effectListData;
    effectList = display.createControl<cList>(&effectListProperties);

    // Waveform
    spectrumData[0].init(600, strTrackerSpectrum::PointsLimit::k300);
    spectrumData[1].init(600, strTrackerSpectrum::PointsLimit::k300);
    strControlProperties spectrumProperties;
    spectrumProperties.x = 0;
    spectrumProperties.y = 75 + ((300 - (MAX_16BIT / 300)) / 2);
    spectrumProperties.w = 600;
    spectrumProperties.h = MAX_16BIT / 300;
    playhead = display.createControl<cProgressCursor>(&spectrumProperties);
    spectrumProperties.data = &spectrumPointsData;
    spectrumPoints = display.createControl<cPoints>(&spectrumProperties);
    spectrumProperties.data = &spectrumData[0];
    spectrumProperties.h = (MAX_16BIT / 300) / 2;
    spectrum[0] = display.createControl<cSpectrum>(&spectrumProperties);
    spectrumProperties.y = 75 + ((300 - (MAX_16BIT / 300)) / 2) + (MAX_16BIT / 300) / 2;
    spectrumProperties.data = &spectrumData[1];
    spectrum[1] = display.createControl<cSpectrum>(&spectrumProperties);

    // background label
    strControlProperties backgroundLabelProperties;
    backgroundLabelProperties.text = nullptr;
    backgroundLabelProperties.colors = nullptr;
    backgroundLabelProperties.value = 255; // 127
    backgroundLabelProperties.style = controlStyleNoTransparency | controlStyleShow;
    backgroundLabelProperties.x = 0;
    backgroundLabelProperties.w = 800;
    backgroundLabelProperties.y = 425;
    backgroundLabelProperties.h = 54;
    bgLabel = display.createControl<cBgLabel>(&backgroundLabelProperties);
}

void cSampleEditor::showMainScreen()
{
    // Bars & Parameter Lists
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(bar[i]);
        display.setControlValue(label[i], 1);
    }
    for (uint8_t i = 0; i < kEffectPageParams; i++)
    {
        display.setControlHide(paramList[i]);
    }

    // Labels
    if (mainScreenButtonPage == 0)
    {
        display.setControlText(label[0], "Start");
        display.setControlText(label[1], "End");
        display.setControlText(label[2], "Zoom");
        showStartPointText();
        showEndPointText();
        showZoomText();
    }
    else
    {
        display.setControlText(label[0], "Load WAV");
        display.setControlText2(label[0], "");
        display.setControlValue(label[0], 0);
        display.setControlText(label[1], "Export WAV");
        display.setControlText2(label[1], "");
        display.setControlValue(label[1], 0);
        display.setControlText(label[2], "");
        display.setControlText2(label[2], "");
        display.setControlValue(label[2], 0);
    }

    display.setControlText(label[3], "Undo");
    display.setControlText2(label[3], "");

    display.setControlText(label[4], "More");
    display.setControlText2(label[4], "");
    display.setControlData(label[4], &labelDataMore);
    display.setControlColors(label[4], display.getTheme().moreButtonColors);
    display.setControlStyle(label[4],
                            controlStyleCenterX | controlStyleCenterY | controlStyleFont3 |
                              controlStyleSpecialFx2);
    labelDataMore.dotIndicator.current = mainScreenButtonPage;
    labelDataMore.dotIndicator.count = 2;

    if (isParametersScreen())
    {
        display.setControlText(label[5], "Apply");
        display.setControlText2(label[5], "");
        display.setControlValue(label[5], 0);
    }
    else
    {
        display.setControlText(label[5], "Select");
        display.setControlText2(label[5], "effect");
    }

    for (uint8_t i = 0; i < 7; i++)
    {
        if (i == 4)
        {
            continue;
        }
        if (mainScreenButtonPage == 1 && i < 3)
        {
            if (i == 0 && mtProject.values.lastUsedInstrument < kSampleInstrumentsCount)
            {
                display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
            }
            else if (i == 1 && editorInstrument->isActive && editorInstrument->sample.length > 0)
            {
                display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
            }
            else
            {
                display.setControlColors(label[i], display.getTheme().inactiveLabelsColors);
            }
            continue;
        }
        if (editorInstrument->isActive)
        {
            if (i != 5)
            {
                display.setControlColors(label[i], display.getTheme().activeLabelsColors);
            }
            else
            {
                display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
            }
        }
        else
        {
            if (i == 5)
            {
                display.setControlColors(
                  label[i],
                  canApplyCurrentEffect() ? display.getTheme().activeButtonLabelsColors
                                          : display.getTheme().inactiveLabelsColors);
            }
            else if (i == 6)
            {
                display.setControlColors(label[i], display.getTheme().activeLabelsColors);
            }
            else
            {
                display.setControlColors(label[i], display.getTheme().inactiveLabelsColors);
            }
        }
    }

    if (editorInstrument->isActive)
    {
        if (!currentEffect->undoParams.isEnable)
        {
            display.setControlColors(label[3], display.getTheme().inactiveLabelsColors);
        }
    }

    for (uint8_t i = 0; i < 6; i++)
    {
        if (i == 5 || (mainScreenButtonPage == 1 && i < 2))
        {
            display.setControlStyle2(label[i],
                                     controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        }
        else
        {
            display.setControlStyle2(label[i],
                                     controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        }

        display.setControlPosition(label[i], (800 / 8) * i + (800 / 16), 424);
        display.setControlSize(label[i], 800 / 8 - 6, 55);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }

    display.setControlValue(label[3], 0);
    display.setControlValue(label[6], 0);
    display.setControlText(label[6], "Effect");
    display.setControlData(label[6], &labelArrow);
    display.setAddControlStyle(label[6], controlStyleShowBitmap);
    display.setControlPosition(label[6], (800 / 4) * 3 + (800 / 8), 424);
    display.setControlSize(label[6], 800 / 4 - 6, 55);
    display.setControlShow(label[6]);
    display.refreshControl(label[6]);

    display.setControlHide(label[7]);
    display.refreshControl(label[7]);
    // List
    refreshEffectList();
    // Waveform

    resizeWaveform();
    showSpectrum();

    showSpectrumPoints();

    hidePlayhead();
    // frame
    if (editorInstrument->isActive || selectedPlace[screenType] == effectButton)
        showFrame();
    else
    {
        display.setControlHide(frame);
        display.refreshControl(frame);
    }

    // title bar
    showTitleBar(paramsScreenLabel);

    // popups
    display.setControlHide(popupProgressBar);
    display.refreshControl(popupProgressBar);

    // hide empty space cover
    display.setControlHide(blankSpaceCover);

    // background label

    display.setControlValue(bgLabel, 0b01111111); // 0b01111111
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);

    display.synchronizeRefresh();
}
void cSampleEditor::showEffectParamsScreen()
{
    int visibleParamsCount = clamp(getCurrentEffectDisplayParams().paramsNumber -
                                     (effectScreenPage * kEffectPageParams),
                                   0,
                                   kEffectPageParams);
    // Bars & Parameter Lists
    for (uint8_t i = 0; i < 8; i++)
    {
        if (i < visibleParamsCount)
        {
            int paramIdx = i + effectScreenPage * kEffectPageParams;
            if (i < kEffectPageParams && isParameterList(paramIdx))
            {
                display.setControlHide(bar[i]);
                display.refreshControl(bar[i]);

                uint8_t count = getParameterListCount(paramIdx);
                int selectedIdx = clamp<int>(
                  (int)(effectDisplayParamsToAdjust[currentEffectIdx].parameter[paramIdx] + 0.5f),
                  0,
                  count > 0 ? (int)count - 1 : 0);

                paramListData[i].length = count;
                paramListData[i].linesCount = std::min<uint16_t>(count, 14);
                paramListData[i].start = selectedIdx;
                paramListData[i].data = getParameterListItems(paramIdx);

                display.setControlData(paramList[i], &paramListData[i]);
                display.setControlValue(paramList[i], selectedIdx);
                display.setControlShow(paramList[i]);
                display.refreshControl(paramList[i]);
            }
            else
            {
                if (i < kEffectPageParams)
                {
                    display.setControlHide(paramList[i]);
                    display.refreshControl(paramList[i]);
                }
                if (isParameterFile(paramIdx))
                {
                    display.setAddControlStyle(bar[i], controlStyleVerticalText | controlStyleFont2);
                    display.setControlText(bar[i], getParameterFileText(paramIdx));
                    display.setControlValue(bar[i], 0);
                }
                else
                {
                    display.setRemoveControlStyle(bar[i], controlStyleVerticalText);
                    display.setControlText(bar[i], nullptr);
                    display.setControlValue(bar[i], paramsBarValue[paramIdx]);
                }
                display.setControlShow(bar[i]);
                display.refreshControl(bar[i]);
            }
        }
        else
        {
            if (i < kEffectPageParams)
            {
                display.setControlHide(paramList[i]);
                display.refreshControl(paramList[i]);
            }
            display.setControlHide(bar[i]);
            display.refreshControl(bar[i]);
        }
    }
    // Labels
    if (isModularPresetPage())
    {
        display.setControlText(label[0], "Load Preset");
        display.setControlText2(label[0], "");
        display.setControlText(label[1], "Save Preset");
        display.setControlText2(label[1], "");
        display.setControlText(label[2], "Reset Params");
        display.setControlText2(label[2], "");
        display.setControlText(label[3], "");
        display.setControlText2(label[3], "");
    }
    else
    {
        for (uint8_t i = 0; i < kEffectPageParams; i++)
        {
            int paramIdx = i + effectScreenPage * kEffectPageParams;

            if (i < visibleParamsCount)
            {
                display.setControlText(
                  label[i], getCurrentEffectDisplayParams().labelsText[paramIdx]);
                display.setControlText2(label[i], parameterValueLabelPtr[paramIdx]);

                if (parameterValueLabelPtr[paramIdx][0] > 0)
                {
                    display.setControlValue(label[i], 1);
                }
                else
                {
                    display.setControlValue(label[i], 1);
                }
            }
            else
            {
                display.setControlText(label[i], "");
                display.setControlText2(label[i], "");
            }
        }
    }

    if (areThereMoreThanOneParamsPage())
    {
        display.setControlText(label[4], "More");
        display.setControlText2(label[4], "");
        display.setControlData(label[4], &labelDataMore);
        display.setControlColors(label[4], display.getTheme().moreButtonColors);
        display.setControlStyle(label[4],
                                controlStyleCenterX | controlStyleCenterY | controlStyleFont3 |
                                  controlStyleSpecialFx2);

        labelDataMore.dotIndicator.current = effectScreenPage;
        labelDataMore.dotIndicator.count = getParamsPagesCount();
    }
    else
    {
        display.setControlColors(label[4], display.getTheme().activeLabelsColors);
        display.setControlText(label[4], "");
        display.setControlText2(label[4], "");
        display.setControlStyle(label[4],
                                controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
    }

    display.setControlValue(label[4], 1);
    display.setControlValue(label[5], 0);
    display.setControlValue(label[6], 0);
    display.setControlValue(label[7], 0);

    display.setControlText(label[5], "Back");
    display.setControlText2(label[5], "");

    switch (previewState)
    {
        case previewStatePreview: display.setControlText(label[6], "Preview"); break;
        case previewStatePlay: display.setControlText(label[6], "Preview"); break;
        case previewStateStop: display.setControlText(label[6], "Stop"); break;
    }
    display.setControlText2(label[6], "");

    display.setControlText(label[7], "Apply");
    display.setControlText2(label[7], "");

    for (uint8_t i = 0; i < 8; i++)
    {
        if (i != 4)
        {
            display.setControlColors(label[i], display.getTheme().activeLabelsColors);
        }
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        if (i != 4)
        {
            display.setControlStyle(label[i],
                                    controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        }
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlPosition(label[i], (800 / 8) * i + (800 / 16), 424);
        display.setControlSize(label[i], 800 / 8 - 6, 55);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }

    // List
    hideEffectList();
    // Waveform
    hideSpectrum();

    hideSpectrumPoints();

    hidePlayhead();
    // frame
    if (isModularPresetPage())
    {
        display.setControlHide(frame);
        display.refreshControl(frame);
    }
    else
    {
        showFrame();
    }

    // title label
    showTitleBar(paramsScreenLabel);

    // popups
    display.setControlHide(popupProgressBar);
    display.refreshControl(popupProgressBar);

    // empty space cover
    display.setControlSize(blankSpaceCover, (800 / 8) * (8 - visibleParamsCount) - 2, -1);
    display.setControlPosition(blankSpaceCover, (800 / 8) * visibleParamsCount + 1, -1);
    display.setControlShow(blankSpaceCover);
    display.refreshControl(blankSpaceCover);

    // background label

    display.setControlValue(bgLabel, 255); // 0b11111111
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);

    display.synchronizeRefresh();
}

void cSampleEditor::showTitleBar(const char* title)
{
    titleBar->show(true);
    titleBar->setTitle(title);

    titleBar->showCurrentInstrumentOnRightLabel();
}

void cSampleEditor::showStartPointText()
{
    if (screenType != mainScreen || mainScreenButtonPage != 0)
        return;
    display.setControlText2(label[0], startPointText);
    display.refreshControl(label[0]);
}
void cSampleEditor::showEndPointText()
{
    if (screenType != mainScreen || mainScreenButtonPage != 0)
        return;
    display.setControlText2(label[1], endPointText);
    display.refreshControl(label[1]);
}
void cSampleEditor::showZoomText()
{
    if (screenType != mainScreen || mainScreenButtonPage != 0)
        return;
    display.setControlText2(label[2], zoomText);
    display.refreshControl(label[2]);
}
void cSampleEditor::showSpectrumPoints()
{
    display.setControlShow(spectrumPoints);
    display.refreshControl(spectrumPoints);
}

void cSampleEditor::hideSpectrumPoints()
{
    display.setControlHide(spectrumPoints);
    display.refreshControl(spectrumPoints);
}

void cSampleEditor::showFrame()
{
    display.setControlValue(frame, selectedPlace[screenType]);
    display.setControlShow(frame);
    display.refreshControl(frame);
}

void cSampleEditor::resizeWaveform()
{
    if (editorInstrument->isActive && isCurrentSampleStereo())
    {
        display.setControlSize(spectrum[0], 600, (MAX_16BIT / 300) / 2);

        display.setControlShow(spectrum[0]);
        display.setControlShow(spectrum[1]);

        display.refreshControl(spectrum[0]);
        display.refreshControl(spectrum[1]);
    }
    else
    {
        display.setControlSize(spectrum[0], 600, (MAX_16BIT / 300));

        display.setControlShow(spectrum[0]);
        display.setControlHide(spectrum[1]);

        display.refreshControl(spectrum[0]);
        display.refreshControl(spectrum[1]);
    }
}

void cSampleEditor::showSpectrum()
{
    display.setControlShow(spectrum[0]);
    display.refreshControl(spectrum[0]);

    if (editorInstrument->isActive && isCurrentSampleStereo())
    {
        display.setControlShow(spectrum[1]);
        display.refreshControl(spectrum[1]);
    }
}

void cSampleEditor::hideSpectrum()
{
    display.setControlHide(spectrum[0]);
    display.setControlHide(spectrum[1]);
    display.refreshControl(spectrum[0]);
    display.refreshControl(spectrum[1]);
}
void cSampleEditor::showPlayhead()
{
    display.setControlShow(playhead);
    display.refreshControl(playhead);
}
void cSampleEditor::hidePlayhead()
{
    display.setControlHide(playhead);
    display.refreshControl(playhead);
}
void cSampleEditor::showEffectList()
{
    display.setControlShow(effectList);
    display.refreshControl(effectList);
}
void cSampleEditor::hideEffectList()
{
    display.setControlHide(effectList);
    display.refreshControl(effectList);
}

void cSampleEditor::showParameterBar(uint8_t n)
{
    int barIdx = n - effectScreenPage * kEffectPageParams;
    if (barIdx < 0 || barIdx >= kEffectPageParams)
        return;

    if (isParameterList(n))
    {
        uint8_t count = getParameterListCount(n);
        int selectedIdx = clamp<int>(
          (int)(effectDisplayParamsToAdjust[currentEffectIdx].parameter[n] + 0.5f),
          0,
          count > 0 ? (int)count - 1 : 0);
        display.setControlValue(paramList[barIdx], selectedIdx);
        display.setControlShow(paramList[barIdx]);
        display.refreshControl(paramList[barIdx]);
    }
    else
    {
        if (isParameterFile(n))
        {
            display.setAddControlStyle(bar[barIdx], controlStyleVerticalText | controlStyleFont2);
            display.setControlText(bar[barIdx], getParameterFileText(n));
            display.setControlValue(bar[barIdx], 0);
        }
        else
        {
            display.setRemoveControlStyle(bar[barIdx], controlStyleVerticalText);
            display.setControlText(bar[barIdx], nullptr);
            display.setControlValue(bar[barIdx], paramsBarValue[n]);
        }
        display.setControlShow(bar[barIdx]);
        display.refreshControl(bar[barIdx]);
    }
}
void cSampleEditor::showParameterLabel(uint8_t n)
{
    int barIdx = n - effectScreenPage * kEffectPageParams;
    if (barIdx < 0 || barIdx >= kEffectPageParams)
        return;

    display.setControlText2(label[barIdx], parameterValueLabelPtr[n]);
    display.setControlShow(label[barIdx]);
    display.refreshControl(label[barIdx]);
}

// Popups

// Applying
void cSampleEditor::showPopupApplying()
{
    display.setControlText(popupProgressBar, "Applying...");
    display.setControlValue(popupProgressBar, applyingProgress);
    display.setControlShow(popupProgressBar);
    display.refreshControl(popupProgressBar);
}
void cSampleEditor::showProgressApplying()
{
    display.setControlValue(popupProgressBar, applyingProgress);
    display.refreshControl(popupProgressBar);
}
// processing
void cSampleEditor::showPopupProcessing()
{
    display.setControlText(popupProgressBar, "Processing...");
    display.setControlValue(popupProgressBar, processingProgress);
    display.setControlShow(popupProgressBar);
    display.refreshControl(popupProgressBar);
}
void cSampleEditor::showProgressProcessing()
{
    display.setControlValue(popupProgressBar, processingProgress);
    display.refreshControl(popupProgressBar);
}
// playing
void cSampleEditor::showPopupPlaying()
{
    display.setControlText(popupProgressBar, "Playing...");
    display.setControlValue(popupProgressBar, playingProgress);
    display.setControlShow(popupProgressBar);
    display.refreshControl(popupProgressBar);
}
void cSampleEditor::showProgressPlaying()
{
    display.setControlValue(popupProgressBar, playingProgress);
    display.refreshControl(popupProgressBar);
}
void cSampleEditor::hideProgressPopup()
{
    display.setControlHide(popupProgressBar);
    display.refreshControl(popupProgressBar);
}

void cSampleEditor::showStaticPopupCommonWindow()
{
    // Bars & Parameter Lists
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(bar[i]);
        display.refreshControl(bar[i]);
    }
    for (uint8_t i = 0; i < kEffectPageParams; i++)
    {
        display.setControlHide(paramList[i]);
        display.refreshControl(paramList[i]);
    }
    // Labels
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 0);
        display.setControlStyle(label[i],
                                controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlPosition(label[i], (800 / 8) * i + (800 / 16), 424);
        display.setControlSize(label[i], 800 / 8 - 6, 55);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlColors(label[i], display.getTheme().activeLabelsColors);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }
    // List
    hideEffectList();
    // Waveform
    hideSpectrum();
    hideSpectrumPoints();
    hidePlayhead();
    // frame

    display.setControlHide(frame);
    display.refreshControl(frame);

    // title label
    if (isParametersScreen())
    {
        strcpy(paramsScreenLabel, "Sample Editor");
    }
    else
    {
        strcpy(paramsScreenLabel, "Sample Editor 1/2");
    }

    showTitleBar(paramsScreenLabel);

    // popups
    display.setControlHide(popupProgressBar);
    display.refreshControl(popupProgressBar);
    // background label

    display.setControlValue(bgLabel, 255); // 0b11111111
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);
}

void cSampleEditor::hideStaticPopup()
{
    mtPopups.hideInfoPopup();
}
// Stop Seq
void cSampleEditor::showPopupStopSequencer()
{
    mtPopups.showInfoPopup("This action will stop the pattern.", "Do you want to continue?");

    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], "Yes");
    display.refreshControl(label[6]);
    display.refreshControl(label[7]);
}
void cSampleEditor::showPopupSeqWindow()
{
    showStaticPopupCommonWindow();
    showPopupStopSequencer();
    display.synchronizeRefresh();
}
// Too Long instrument
void cSampleEditor::showPopupTooLongSample()
{
    mtPopups.showInfoPopup("Selected sample is too long.", "Choose another one.");

    display.setControlText(label[7], "Ok");
    display.refreshControl(label[7]);
}
void cSampleEditor::showPopupTooLongSampleWindow()
{
    showStaticPopupCommonWindow();
    showPopupTooLongSample();
    display.synchronizeRefresh();
}

// Too Long processed sample
void cSampleEditor::showPopupTooLongProcessedSample()
{
    mtPopups.showInfoPopup("After applying changes,the sample will be too long.",
                           "Change the effect parameters and try again.");
    display.setControlText(label[7], "Ok");
}
void cSampleEditor::showPopupTooLongProcessedSampleWindow()
{
    showStaticPopupCommonWindow();
    showPopupTooLongProcessedSample();
    display.synchronizeRefresh();
}
//************* play/stop/preview
void cSampleEditor::showPreviewLabel()
{
    display.setControlText(label[6], "Preview");
    display.refreshControl(label[6]);
}
void cSampleEditor::showPlayLabel()
{
    display.setControlText(label[6], "Preview");
    display.refreshControl(label[6]);
}
void cSampleEditor::showStopLabel()
{
    display.setControlText(label[6], "Stop");
    display.refreshControl(label[6]);
}

//==============================================================================================================
// schowanie zbędnych kontrolek przy np. pokazywaniu listy instrumentów
void cSampleEditor::hideUselessControls()
{
    display.setControlHide(spectrumPoints);
    display.setControlHide(effectList);
    display.setControlHide(playhead);
    display.setControlHide(spectrum[0]);
    display.setControlHide(spectrum[1]);
}
