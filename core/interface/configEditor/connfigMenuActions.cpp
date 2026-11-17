
#include <string.h>

#include "configEditor/configEditor.h"
#include "configEditor/configMenu.h"

#include "fileManager.h"

#include "mtAudioEngine.h"
#include "mtPadBoard.h"

#include "Platform.h"
#include "configEditor/configMenuActions.h"
#include "ThemeManager.h"

#include "Tracker.h"
#include "core/interface.h"

// device startup actions after Config read
void initConfigValuesAfterReadConfig()
{
    usbAudioChange();
    setDisplayBrightness();
}

// reset config actions
void restartConfigAllActions()
{
    usbAudioChange();

    setRadioRegion();
    setDisplayBrightness();
    setPadsBrightness();

    setPadBoardValues();

    setAntialiasingEnable();

    setMetronomValues();
}

void setRadioRegion()
{
    if (mtConfig.general.radioRegion > 3)
        mtConfig.general.radioRegion = 0;

    mtInterface.getPlatform()->getRadio()->setRegion((Radio::Region)mtConfig.general.radioRegion);
}

void setDisplayBrightness()
{
    if (mtConfig.general.dispBrightness > 2)
        mtConfig.general.dispBrightness = 2;

    switch (mtConfig.general.dispBrightness)
    {
        case 0: display.setBacklightBrightness(20); break;
        case 1: display.setBacklightBrightness(50); break;
        case 2: display.setBacklightBrightness(128); break;
        default: break;
    }
}

void setDisplayTheme()
{
    if (mtConfig.general.theme >= ThemeManager::getThemeCount())
        mtConfig.general.theme = DisplayThemes::kOriginal;
    ThemeManager::applyTheme(mtConfig.general.theme);
    mtInterface.deactivateModule(&configEditor);
    mtInterface.activateModule(&configEditor, 0);
}

void setDisplayFont()
{
    if (mtConfig.general.font >= ThemeManager::getFontCount())
        mtConfig.general.font = 0;
    ThemeManager::applyFont(mtConfig.general.font);
    mtInterface.deactivateModule(&configEditor);
    mtInterface.activateModule(&configEditor, 0);
}

void setPadsBrightness()
{
    if (mtConfig.general.padsBrightness > 2)
        mtConfig.general.padsBrightness = 2;

    mtConfig.values.padsLightBack =
      (PADS_LIGHT_BACK_DEFAULT * (mtConfig.general.padsBrightness + 1)) / 3;
    mtConfig.values.padsLightFront =
      (PADS_LIGHT_FRONT_DEFAULT * (mtConfig.general.padsBrightness + 1)) / 3;
    mtConfig.values.padsLightBackDim =
      ((PADS_LIGHT_BACK_DEFAULT / 2) * (mtConfig.general.padsBrightness + 1)) / 3;
}

void setPadBoardValues()
{
    mtPadBoard.setPadNotes(mtProject.values.padBoardScale,
                           mtProject.values.padBoardNoteOffset,
                           mtProject.values.padBoardRootNote);
    newFileManager.setProjectStructChanged();
}

void setAntialiasingEnable()
{
    engine.setInterpolationEnable(mtProject.values.antialiasingEnable);
    newFileManager.setProjectStructChanged();
}

void setLimiterMode()
{
    engine.setLimiterAttack(mtProject.values.limiterAttack);
    engine.setLimiterRelease(mtProject.values.limiterRelease);
    newFileManager.setProjectStructChanged();
}

void setSendsMode()
{
    engine.refreshAllSends();
    newFileManager.setProjectStructChanged();
}

void setHeadroomMode()
{
    engine.setHeadroomEnable((bool)mtProject.values.isHeadroomEnabled);
    newFileManager.setProjectStructChanged();
}

void setSnapToZeroMode()
{
    engine.setSnapToZeroEnable((bool)mtProject.values.isSnapToZeroEnabled);
    newFileManager.setProjectStructChanged();
}

void setMetronomValues()
{
    // wlaczenie wylaczenie metronomu
    // aktualizajca metrum
    // volume mozna brac pod uwage na bierzaco tam gdzie przetwarzanie metronomu

    // mtConfig.metronome.state;
    // mtConfig.metronome.timeSignature;
}

void setSlaveClockCompValue()
{
    sequencer.setExternalClockComp(mtConfig.midi.slaveClockComp);
}

void resetConfigShowPopup()
{
    configEditor.showResetConfigPopout();
    configEditor.ip->blockAllInputsExcept(interfaceButton6, interfaceButton7);
}

void usbStorageEnter()
{
    startFileTransferMode();
}

void usbAudioChange()
{
    Usb* usb = mtInterface.getPlatform()->getUsb();

    switch (mtConfig.common.usbAudioMode)
    {
        case 1:
            usb->setClassState(Usb::ClassType::kAudioIn, true);
            usb->setClassState(Usb::ClassType::kAudioOut, false);
            break;
        case 2:
            usb->setClassState(Usb::ClassType::kAudioIn, false);
            usb->setClassState(Usb::ClassType::kAudioOut, true);
            break;
        case 3:
            usb->setClassState(Usb::ClassType::kAudioIn, true);
            usb->setClassState(Usb::ClassType::kAudioOut, true);
            break;
        default:
            usb->setClassState(Usb::ClassType::kAudioIn, false);
            usb->setClassState(Usb::ClassType::kAudioOut, false);
            break;
    }
    engine.refreshOutputSourceOutsideTheRecorder();
    usb->reset();
}

void checkFuncitonKey(int key)
{
    for (int idx = 0; idx < 8; idx++)
    {
        if (idx != key && mtConfig.interface.functionKeysBinding[idx] ==
                            mtConfig.interface.functionKeysBinding[key])
        {
            int firstFreeFunction = 0;
            bool assigned;

            for (uint8_t functionIdx = 0; functionIdx < kMenuModulesCount; functionIdx++)
            {
                assigned = false;
                for (uint8_t keyIdx = 0; keyIdx < 8; keyIdx++)
                {
                    if (mtConfig.interface.functionKeysBinding[keyIdx] == functionIdx)
                    {
                        assigned = true;
                    }
                }

                if (!assigned)
                {
                    firstFreeFunction = functionIdx;
                    break;
                }
            }

            mtConfig.interface.functionKeysBinding[idx] = firstFreeFunction;
        }
    }
}

void setFunctionKey1()
{
    checkFuncitonKey(0);
}

void setFunctionKey2()
{
    checkFuncitonKey(1);
}

void setFunctionKey3()
{
    checkFuncitonKey(2);
}

void setFunctionKey4()
{
    checkFuncitonKey(3);
}

void setFunctionKey5()
{
    checkFuncitonKey(4);
}

void setFunctionKey6()
{
    checkFuncitonKey(5);
}

void setFunctionKey7()
{
    checkFuncitonKey(6);
}

void setFunctionKey8()
{
    checkFuncitonKey(7);
}
