#ifndef INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_
#define INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_

void restartConfigAllActions();
void initConfigValuesAfterReadConfig();

void setRadioRegion();
void setDisplayBrightness();
void setDisplayTheme();
void setDisplayFont();
void refreshThemeAndFontMenuOptions();
void setPadsBrightness();

void setPadBoardValues();
void setAntialiasingEnable();
void setLimiterMode();
void setSendsMode();
void setHeadroomMode();
void setSnapToZeroMode();

void setMetronomValues();
void setSlaveClockCompValue();

void openCreditsAction();

void startFileTransferMode();

void openManual1Action();
void openManual2Action();
void openManual3Action();

void setFunctionKey1();
void setFunctionKey2();
void setFunctionKey3();
void setFunctionKey4();
void setFunctionKey5();
void setFunctionKey6();
void setFunctionKey7();
void setFunctionKey8();

void resetConfigShowPopup();
void usbStorageEnter();
void usbAudioChange();

#endif /* INTERFACE_CONFIGEDITOR_CONFIGMENUACTIONS_H_ */
