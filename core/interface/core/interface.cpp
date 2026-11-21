#include <string.h>

#include "core/interface.h"
#include "ThemeManager.h"
#include "configEditor/configEditor.h"
#include "configEditor/configMenuActions.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "core/titleBar.h"

#include "fileBrowser/GlobalFileBrowser.h"
#include "imageViewer/imageViewer.h"
#include "instrumentEditor/instrumentEditor.h"
#include "masterParams/masterParams.h"
#include "patternEditor/patternEditor.h"
#include "performanceMode/performanceMode.h"
#include "projectEditor/projectEditor.h"
#include "sampleEditor/sampleEditor.h"
#include "sampleImporter/sampleImporter.h"
#include "samplePlayback/samplePlayback.h"
#include "sampleRecorder/sampleRecorder.h"
#include "songEditor/songEditor.h"

#include "fileManager.h"
#include "mtConfig.h"
#include "mtPadBoard.h"
#include "mtStructs.h"
#include "mtTest.h"

#include "Platform.h"
#include "VoiceStreamManager.h"
#include "config.h"
#include "log.h"
#include "mtHardware.h"
#include "mtSleep.h"

InputProcessor ip(mtInterfacePotsDef::interfacePotsCount,
                  mtInterfaceButtonsDef::interfaceButtonsCount,
                  mtInterfacePadsDef::interfacePadsCount);
Interface mtInterface;

strMtConfig mtConfig;
OCRAM_BSS_SECTION strMtProject mtProject;
InterfaceGlobals interfaceGlobals;
strGrid mtGrid;

extern void unmuteAudioOutByKey();
extern void muteAudioOutByKey();
extern uint8_t isAudioOutMuted();

//=======================================================================
//							SETAP INTERFEJSU
//=======================================================================
//=======================================================================

const uint8_t Interface::modulesCount = 13;
const hModule Interface::modules[modulesCount] = {
    &projectEditor,    // 0
    &sampleImporter,   // 1
    &patternEditor,    // 2
    &samplePlayback,   // 3
    &songEditor,       // 4
    &instrumentEditor, // 5
    &sampleEditor,     // 6
    &configEditor,     // 7
    &sampleRecorder,   // 8
    &performanceMode,  // 9
    &imageViewer,       // 10
    &globalFileBrowser, // 11
    &masterParams,      // 12
};

const uint8_t Interface::modulesButtonsCount = 11;
const uint32_t Interface::modulesButtons[modulesButtonsCount][3] = {
    { interfaceButtonPerformance, 9, 0 },
    { interfaceButtonMaster, 12, 0 },
    { interfaceButtonParams, 5, mtInstEditModeParams },
    { interfaceButtonFile, 0, 0 },
    { interfaceButtonPattern, 2, 0 },
    { interfaceButtonSamplePlay, 3, 0 },
    { interfaceButtonSampleEdit, 6, 0 },
    { interfaceButtonSampleRec, 8, 0 },
    { interfaceButtonSampleLoad, 1, 0 },
    { interfaceButtonSong, 4, 0 },
    { interfaceButtonConfig, 7, 0 },
};

//=======================================================================
//=======================================================================
//=======================================================================
//=======================================================================

Platform* Interface::getPlatform()
{
    return platform;
}

void Interface::begin(Platform* platform, Synth* synth)
{
    Interface::platform = platform;
    Interface::fileSystem = platform->getFileSystem();
    Interface::ip = &::ip;
    patternEditor.initExternalRamState();
    Interface::synth = synth;
    operatingMode = mtOperatingModeStartup;

    buttons.init(platform->getButtons());
    titleBar.init(synth, platform->getBattery());

    ModuleBase::setPlatform(platform);
    ModuleBase::setEventFunct(interfaceEnvents);
    ModuleBase::setInputProcessor(ip);
    ModuleBase::setButtons(&buttons);
    ModuleBase::setTitleBar(&titleBar);
    ModuleBase::setSynth(synth);

    mtTest.setCodecAudio(platform->getCodecAudio());
    mtTest.setRadio(platform->getRadio());
    mtTest.setButtons(platform->getButtons());
    mtTest.setEncoder(platform->getEncoder());
    mtTest.setTimer(platform->getTimer());
    mtTest.setBattery(platform->getBattery());
    mtTest.setFileSystem(platform->getFileSystem());

    ip->unblockAllInputs();
}

//=======================================================================
//=======================================================================
//=======================================================================
void Interface::update()
{
    processOperatingMode();

    if (onScreenModule != nullptr && onScreenModule->moduleRefresh)
        onScreenModule->update();

    mtPopups.update();

    mtTest.testLoop();

    commonThingsUpdate();
}

//=======================================================================
//=======================================================================
//=======================================================================

void Interface::processOperatingMode()
{
    switch (operatingMode)
    {
        case mtOperatingModeStartup:
        {
            doStartTasks(); // odczyt configu, wersji, init controlek popupow, ekran startowy/braku
                            // karty
            operatingMode = mtOperatingModeOpenProject;
            break;
        }
        case mtOperatingModeOpenProject:
        {
            if (fileSystem->isSdCardInserted())
            {
                hideDisplayNoSdCard();
                openStartupProject();
                operatingMode = mtOperatingModeShowStartScreen;
            }
            break;
        }
        case mtOperatingModeShowStartScreen:
        {
            processStartScreen();
            break;
        }
        case mtOperatingModePowerOffSequence:
        {
            processPowerOffSequence();
            break;
        }
        case mtOperatingModeSleep:
        {

            break;
        }
        case mtOperatingModeRun:
        {
            if (isAudioOutMuted())
            {
                unmuteAudioOutByKey();
            }
            updateConfig(false);
            break;
        }
        default: break; // mtOperatingModeRun
    }
}

void Interface::doStartTasks()
{
    readConfig();

    sequencer.initAfterReadConfig();
    initConfigValuesAfterReadConfig();

    if (mtConfig.firmware.beta > 0 && mtConfig.firmware.beta < 255)
    {
        sprintf(interfaceGlobals.currentFirmwareVersion,
                firmwareVersionLabelFormatBeta,
                PRODUCT_VER_1,
                PRODUCT_VER_2,
                PRODUCT_VER_3,
                PRODUCT_BETA);
    }
    else
    {
        sprintf(interfaceGlobals.currentFirmwareVersion,
                firmwareVersionLabelFormat,
                PRODUCT_VER_1,
                PRODUCT_VER_2,
                PRODUCT_VER_3);
    }
    interfaceGlobals.currentFirmwareBuild = platform->getSystemInfo()->buildId;

    ThemeManager::init(platform);
    randomNameGenerator.init(platform->getFileSystem());
    refreshThemeAndFontMenuOptions();

    songTimer.initControls();
    mtPopups.initPopupsDisplayControls();
    mtMenuPopup.initPopupsDisplayControls();
    titleBar.initControls();
    ModuleBase::keyboardManager.initControls();
    ModuleBase::keyboardManager.init(platform->getLeds(), platform->getTimer());

    ThemeManager::applyTheme(mtConfig.general.theme);
    ThemeManager::applyFont(mtConfig.general.font);
    initStartScreen();

    if (!fileSystem->isSdCardInserted())
    {
        hideStartScreen();
        showDisplayNoSdCard();
    }

    display.processDisplayUpdateImmediate();

    engine.setHeadphonesVolume(mtConfig.audioCodecConfig.volume);
    engine.setLineInOutType(AudioStereoToMono::OutSignalType::stereo);
    engine.setPreviewVolume(mtConfig.general.previewVolume);
}

void Interface::processStartScreen()
{
    if (fileSystem->isSdCardInserted())
    {
        refreshStartScreen();

        if (detectProjectLoadState())
        {
            // koniec ekranu startowego
            destroyStartScreen();
            hideDisplayNoSdCard();
            operatingMode = mtOperatingModeRun;
            display.setModulesControlDestroyPoint();
#ifdef TRACKER_MINI
            initSpecialMiniTrackerButtons();
#endif
            activateModule(&patternEditor, 0);
            unmuteAudioOutByKey();

#ifdef TRACKER_MINI
            platform->getBattery()->enableMonitoring();
#endif
        }
    }
    else
    {
        hideStartScreen();
        showDisplayNoSdCard();
        operatingMode = mtOperatingModeOpenProject;
    }
}

void Interface::processPowerOffSequence()
{
    if (lowPower.getLowPowerState() == shutdownStateStart)
    {
        refreshDisplayShutdown();

        if (shutdownSaveFlag == 0 && lowPower.getShutdownProgress() > 10 &&
            lowPower.getShutdownProgress() < 70)
        {
            if (newFileManager.saveProjectToWorkspace())
            {
                shutdownSaveFlag = 1;
            }
        }
        if (lowPower.getShutdownProgress() < 15 && !isAudioOutMuted())
        {
            muteAudioOutByKey();
        }

        if (lowPower.getTimeLeft() == 0 && newFileManager.getStatus() == fmIdle)
        {
            operatingMode = mtOperatingModeSleep;
            lowPower.goLowPower();
        }
    }
}

void Interface::handleCardSlotAction(bool state)
{
    if (operatingMode != mtOperatingModeRun)
    {
        return;
    }

    if (state)
    {
        activateInterface();
    }
    else
    {
        if (specialScreenMode == usbStorageScreenMode)
        {
            ip->unblockAllInputs();
            handleFileTransferMode(0);
        }

        activateNoSdScreen();
    }
}

void Interface::handlePowerButtonAction(uint8_t state) // 1 = przycisk wcisniety
{
    if (state) // press
    {
        if (operatingMode != mtOperatingModeRun && fileSystem->isSdCardInserted())
        {
            return;
        }

        operatingMode = mtOperatingModePowerOffSequence;
        lowPower.startPowerOffSequence();

        if (!fileSystem->isSdCardInserted()) // jeseli wyjeta karta sd
        {
            hideDisplayNoSdCard();

            activateShutdownMode();
        }
        else // normalnie
        {
            activateShutdownMode();
        }
    }
    else // relase
    {
        if (operatingMode != mtOperatingModePowerOffSequence)
            return;

        shutdownSaveFlag = 0;
        lowPower.stopPowerOffSequence();
        operatingMode = mtOperatingModeRun;

        // specjalne zachowanie w zaleznosci od stanu sd
        if (fileSystem->isSdCardInserted())
        {
            activateInterface();
        }
        else
        {
            hideDisplayShutdown();
            showDisplayNoSdCard();
        }
    }
}

void Interface::handleFileTransferMode(uint8_t state)
{
    if (operatingMode != mtOperatingModeRun)
    {
        return;
    }

    if (state)
    {
        // The host is about to own the card: playback must not keep streaming
        // samples from it, and live recording must not leave changes behind
        // for autosave to write to it. The armed state covers the pre-roll.
        if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP || sequencer.isRec())
        {
            sequencer.stop();
        }
        sequencer.stopManualNotes();

        // Also the check that no other file operation is using the card.
        if (!newFileManager.flushWorkspaceSave())
        {
            return;
        }

        activateFileTransferModeScreen();

        // Closing the module above may have queued more to save. After this
        // the device must not use the card until the mode ends: not for
        // saving, and not for streaming the notes that are still sounding or
        // that MIDI input starts.
        updateConfig(true);
        newFileManager.flushWorkspaceSave();
        newFileManager.setWorkspaceSaveSuspended(true);
        voiceStreamManager.setSuspended(true);
        LOG("USB storage mode on");
    }
    else
    {
        LOG("USB storage mode off");

        // The host may have changed the card, so what the filesystem driver
        // and sample streaming remember about it can be wrong. When the mode
        // ends because the card was ejected, there is nothing to mount.
        if (fileSystem->remount())
        {
            voiceStreamManager.rebindAllInstrumentStreamFiles();
        }
        voiceStreamManager.setSuspended(false);
        newFileManager.setWorkspaceSaveSuspended(false);
        activateInterface();
    }

    Usb* usb = mtInterface.getPlatform()->getUsb();
    usb->setClassState(Usb::ClassType::kStorage, state == 1 ? true : false);
    usb->reset();
}

bool Interface::setFileTransferMode(bool enable)
{
    bool active = specialScreenMode == usbStorageScreenMode;

    if (enable != active)
    {
        if (!enable)
        {
            ip->unblockAllInputs();
        }

        handleFileTransferMode(enable ? 1 : 0);
    }

    return specialScreenMode == usbStorageScreenMode;
}

void Interface::refreshInterfaceControlsColors()
{
    songTimer.refreshColors();
    mtPopups.refreshColors();
    mtMenuPopup.refreshColors();
    titleBar.refreshColors();
    ModuleBase::keyboardManager.refreshColors();
}

//=======================================================================
//=======================================================================
//=======================================================================
void Interface::activateModule(hModule module, uint32_t options)
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    onScreenModule = module;
    if (module != &patternEditor)
    {
        songTimer.show();
    }
    module->initDisplayControls();

    module->start(options);
    lastOptions = options;
}

void Interface::deactivateModule(hModule module)
{
    if (mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift))
    {
        ip->processButtonsInput(interfaceButtonShift, buttonRelease);
    }

    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    module->stop();
    if (ModuleBase::keyboardManager.getState())
    {
        ModuleBase::keyboardManager.deactivateKeyboard();
    }
    mtPopups.hideStepPopups();
    songTimer.hide();
    titleBar.hide();
    display.resetControlQueue();
    display.destroyModulesControls();

    if (module == onScreenModule)
    {
        onScreenModule = nullptr;
    }

    previousModule = module;
    previousModuleOptions = lastOptions;
}

hModule Interface::getPreviousModule() const
{
    return previousModule;
}

uint32_t Interface::getPreviousModuleOptions() const
{
    return previousModuleOptions;
}

void Interface::setPreviousModule(hModule module, uint32_t options)
{
    previousModule = module;
    previousModuleOptions = options;
}

void Interface::switchModuleByButton(hModule module, uint8_t button, uint32_t options)
{
    int8_t index = getButtonIndex(button);
    if (index < 0)
        return;
    if (modules[modulesButtons[index][1]] == module)
        return;

    uint32_t localOptions = options > 0 ? options : modulesButtons[index][2];

    deactivateModule(module);
    activateModule(modules[modulesButtons[index][1]], localOptions);
}

void Interface::switchModuleToPrevious(hModule module, uint8_t options)
{
    if (previousModule == module)
        return;

    hModule prevModule = previousModule;
    uint32_t prevModuleOptions = previousModuleOptions;

    uint32_t localOptions = options > 0 ? options : prevModuleOptions;

    deactivateModule(module);
    activateModule(prevModule, localOptions);
}

int8_t Interface::getButtonIndex(uint8_t button)
{
    for (uint8_t i = 0; i < modulesButtonsCount; i++)
    {
        if (button == modulesButtons[i][0])
        {
            return i;
        }
    }
    return -1;
}

void Interface::toggleActiveModule(uint8_t state)
{
    if (state)
    {
        if (toggledState && onScreenModule == nullptr)
        {
            toggledState = 0;

            if (previousModule != nullptr)
            {
                hModule prevModule = previousModule;
                uint32_t prevModuleOptions = previousModuleOptions;

                activateModule(prevModule, prevModuleOptions);

                // Hiding the module recorded it as its own predecessor. Put back the one
                // it had, or it can no longer switch back to the previous module.
                previousModule = moduleBeforeToggle;
                previousModuleOptions = moduleBeforeToggleOptions;
                return;
            }
        }
    }
    else
    {
        if (onScreenModule != nullptr)
        {
            moduleBeforeToggle = previousModule;
            moduleBeforeToggleOptions = previousModuleOptions;

            mtInterface.deactivateModule(onScreenModule);
            toggledState = 1;
        }
    }
}

bool Interface::isSongScreenActive()
{
    return isOnScreenModule(&songEditor);
}

bool Interface::isPatternScreenActive()
{
    return isOnScreenModule(&patternEditor);
}

bool Interface::isPerformanceModeScreenActive()
{
    return isOnScreenModule(&performanceMode);
}

bool Interface::isMasterScreenActive()
{
    return isOnScreenModule(&masterParams);
}

bool Interface::isInstrumentParamsScreenActive()
{
    return isOnScreenModule(&instrumentEditor);
}

bool Interface::isSamplePlayerScreenActive()
{
    return isOnScreenModule(&samplePlayback);
}

bool Interface::isOnScreenModule(ModuleBase* module)
{
    return onScreenModule == module;
}

//=======================================================================
//=======================================================================
//=======================================================================

void interfaceEnvents(uint8_t event, void* param1, void* param2, void* param3)
{
    switch (event)
    {
        case eventSwitchModule:
        {
            // param1 = uchwyt do modułu, param2 = index przycisku
            mtInterface.switchModuleByButton(
              (hModule)param1, *((uint8_t*)param2), param3 == nullptr ? 0 : *((uint32_t*)param3));
            break;
        }
        case eventSwitchToPreviousModule:
        {
            // param1 = handle to source module, param2 = start options
            mtInterface.switchModuleToPrevious((hModule)param1,
                                               param2 == nullptr ? 0 : *((uint8_t*)param2));
            break;
        }
        case eventActivateGameModule:
        {
            break;
        }
        case eventActivateImageViewer:
        {
            mtInterface.deactivateModule((hModule)param1);
            mtInterface.activateModule(&imageViewer, param2 == nullptr ? 0 : *((uint8_t*)param2));
            break;
        }
        case eventToggleActiveModule:
        {
            mtInterface.toggleActiveModule(*((uint8_t*)param3));
            break;
        }
        case eventActivateTestingProcedure:
        {
            mtInterface.deactivateModule((hModule)param1);
            mtTest.runTestingProcedure(mtInterface.ip);
            break;
        }
        case eventSetFileTransferMode:
        {
            mtInterface.handleFileTransferMode(*((uint8_t*)param1));
            break;
        }
        default: break;
    }
}
