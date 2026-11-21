#include "configEditor/configMenu.h"
#include "configEditor/configEditor.h"

#include "configEditor/configMenuActions.h"
#include "ThemeManager.h"

#include "imageViewer/imageViewerDefs.h"

#include "polyassert.h"

#include <array>

#ifdef TRACKER_MINI
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#endif

// clang-format off

// baza baz
cMenuGroup menuBase(menuBase, 0);
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================

// grupy glowne

cMenuGroup menuProject(menuBase, "Project Settings");
cMenuGroup menuGeneral(menuBase, "General");
cMenuGroup menuMidi(menuBase, "MIDI");

cMenuGroup menuUsb(menuBase, "USB");

cMenuGroup menuMetro(menuBase, "Metronome");
cMenuGroup menuFirmware(menuBase, "Firmware");

#ifdef TRACKER_MINI
cMenuGroup menuFunctionButtons(menuBase, "Fn buttons binding");
#endif

cMenuGroup menuCredits(menuBase, "Info");

// elementy/////////

// project settings
const strItemTypeListValues setupPatternDiv{ &mtProject.values.patternDiv, 0, 15, 1, 1 };
const strItemTypeListText setupPerfSource{ &mtConfig.general.performanceSource, 2, performanceSourceTexts };
const strItemTypeListTextWithAction setupPadsScale{ &mtProject.values.padBoardScale, 39, scaleNames, setPadBoardValues };
const strItemTypeListTextWithAction setupPadsRootNote{ &mtProject.values.padBoardRootNote,  37,  &allNotes[12], setPadBoardValues };
const strItemTypeListTextWithAction setupPadsLayout{ &mtProject.values.padBoardNoteOffset, 12, padsLayoutTexts, setPadBoardValues };
const strItemTypeListTextWithAction setupAntialiasing{ &mtProject.values.antialiasingEnable, 2, onOffStateTexts, setAntialiasingEnable };
const strItemTypeListTextWithAction setupLimiterMode{ &mtProject.values.limiterMode, 2, limiterModeTexts, setLimiterMode };
const strItemTypeListTextWithAction setupSendsMode{ &mtProject.values.sendsMode, 2, sendsModeTexts, setSendsMode };
const strItemTypeListTextWithAction setupHeadroomMode{ &mtProject.values.isHeadroomEnabled, 2, onOffStateTexts, setHeadroomMode };
const strItemTypeListTextWithAction setupSnapToZeroMode{ &mtProject.values.isSnapToZeroEnabled, 2, onOffStateTexts, setSnapToZeroMode };

cMenuItem melPerformanceSource(menuProject, "Performance presets", perfPresDesc, menuTypeItemListText, &setupPerfSource);
cMenuItem melPatternDiv(menuProject, "Pattern divider", pattDivDesc, menuTypeItemListValues, &setupPatternDiv);
cMenuItem melPadsScale(menuProject, "Notes scale", scaleDesc,  menuTypeItemListTextWithAction, &setupPadsScale);
cMenuItem melPadsRootNote(menuProject,  "Root note", rootNoteDesc, menuTypeItemListTextWithAction,  &setupPadsRootNote);
cMenuItem melPadsLayout(menuProject, "Notes layout", padLayDesc, menuTypeItemListTextWithAction, &setupPadsLayout);
cMenuItem melAntialiasingEnable(menuProject, "Anti-aliasing", antialDesc, menuTypeItemListTextWithAction, &setupAntialiasing);
cMenuItem melLimiterMode(menuProject, "Limiter mode", limiterDesc, menuTypeItemListTextWithAction, &setupLimiterMode);
cMenuItem melSendsMode(menuProject, "Sends mode", sendsDesc,  menuTypeItemListTextWithAction, &setupSendsMode);
cMenuItem melHeadroomMode(menuProject, "Extra headroom", headroomDesc,  menuTypeItemListTextWithAction, &setupHeadroomMode);
cMenuItem melSnapToZeroMode(menuProject, "Snap to zero", snapToZeroDesc,  menuTypeItemListTextWithAction, &setupSnapToZeroMode);

// general
const strItemTypeListTextWithAction setupRadioRegion{ &mtConfig.general.radioRegion, 4, radioRegion, setRadioRegion };
const strItemTypeListTextWithAction setupDispBrightness{ &mtConfig.general.dispBrightness, 3, brightness, setDisplayBrightness };
strItemTypeListTextWithAction setupDispTheme{ &mtConfig.general.theme, DisplayThemes::kCount, themeChoices, setDisplayTheme };
strItemTypeListTextWithAction setupDispFont{ &mtConfig.general.font, 2,  ptrFont, setDisplayFont };

void refreshThemeAndFontMenuOptions()
{
    setupDispTheme.count = ThemeManager::getThemeCount();
    setupDispTheme.ptrText = ThemeManager::getThemeNames();
    setupDispFont.count = ThemeManager::getFontCount();
    setupDispFont.ptrText = ThemeManager::getFontNames();
}

const strItemTypeListTextWithAction setupPadsBrightness{ &mtConfig.general.padsBrightness, 3, brightness, setPadsBrightness };
const strItemTypeListText setupRecQuantization{ &mtConfig.general.recOptions, 4, recOptionsTexts };
const strItemTypeListText setupRecThreshold{ &mtConfig.general.recThreshold, recThreshold_count, recThresholdTexts };
const strItemTypeListText setupHexMode{ &mtConfig.general.numberingMode, std::size(numberingModeTexts), numberingModeTexts };
const strItemTypeListText setupPadPreviewInPatternEdit{ &mtConfig.general.padPreviewInPatternEdit, 3, padPreviewInPatternEditTexts };
const strItemTypeListText setupPatternArrangement{ &mtConfig.general.patternArrangement, 3, patternArrangementTexts };
const strItemTypeListText setupPatternInfo{ &mtConfig.general.patternInfo, 3, patternInfoTexts };
const strItemTypeListText setupNoteDisplayMode{ &mtConfig.general.noteDisplayMode, std::size(noteDisplayModeTexts), noteDisplayModeTexts };

#ifndef TRACKER_MINI
cMenuItem melRadioReg(menuGeneral,
                      "Radio region",
                      radioRegDesc,
                      menuTypeItemListTextWithAction,
                      &setupRadioRegion);
#endif
cMenuItem melDispBright(menuGeneral,
                        "Display brightness",
                        displBrightDesc,
                        menuTypeItemListTextWithAction,
                        &setupDispBrightness);
cMenuItem melDispTheme(menuGeneral,
                       "Display theme",
                       displThemeDesc,
                       menuTypeItemListTextWithAction,
                       &setupDispTheme);
cMenuItem melDispFont(menuGeneral,
                      "Display font",
                      displFontDesc,
                      menuTypeItemListTextWithAction,
                      &setupDispFont);

#ifndef TRACKER_MINI
cMenuItem melPadsBright(menuGeneral,
                        "Pads brightness",
                        padsBrightDesc,
                        menuTypeItemListTextWithAction,
                        &setupPadsBrightness);
cMenuItem melPadPreview(menuGeneral,
                        "Pads preview",
                        padsPrevDesc,
                        menuTypeItemListText,
                        &setupPadPreviewInPatternEdit);
#endif
cMenuItem melRecQuantization(menuGeneral,
                             "Recording options",
                             recNotesDesc,
                             menuTypeItemListText,
                             &setupRecQuantization);
cMenuItem melRecThreshold(menuGeneral,
                          "Recording Threshold",
                          recThresholdDesc,
                          menuTypeItemListText,
                          &setupRecThreshold);
cMenuItem melHexMode(menuGeneral,
                     "Numbering mode",
                     hexModeDesc,
                     menuTypeItemListText,
                     &setupHexMode);
cMenuItem melPatternArrangement(menuGeneral,
                                "Pattern arrangement",
                                patternArraDesc,
                                menuTypeItemListText,
                                &setupPatternArrangement);
cMenuItem melPatternInfo(menuGeneral,
                         "Pattern top info",
                         patternInfo,
                         menuTypeItemListText,
                         &setupPatternInfo);
cMenuItem melNoteDisplayMode(menuGeneral,
                             "Note display",
                             noteDisplayModeDesc,
                             menuTypeItemListText,
                             &setupNoteDisplayMode);

//==================================================================================================================================================================
// Metroneome
const strItemTypeListTextWithAction setupMetroState{ &mtConfig.metronome.state, 2, onOffStateTexts, setMetronomValues };
const strItemTypeListTextWithAction setupMetroPreRoll{ &mtConfig.metronome.preRoll, 2, onOffStateTexts, setMetronomValues };
const strItemTypeListTextWithAction setupMetroTimeSigNum{ &mtConfig.metronome.timeSignatureNumerator, 12, metroTimeSig, setMetronomValues };
const strItemTypeListTextWithAction setupMetroTimeSigDenum{ &mtConfig.metronome.timeSignatureDenominator, 12, metroTimeSig, setMetronomValues };
const strItemTypeListValues setupMetroVolume{ &mtConfig.metronome.volume, 0, 100, 1, 0 };

cMenuItem melMetroState(menuMetro,
                        "State",
                        nullptr,
                        menuTypeItemListTextWithAction,
                        &setupMetroState);
cMenuItem melMetroPreRoll(menuMetro,
                          "Pre roll",
                          nullptr,
                          menuTypeItemListTextWithAction,
                          &setupMetroPreRoll);
cMenuItem melMetroTimeSigNum(menuMetro,
                             "Time signature num",
                             nullptr,
                             menuTypeItemListTextWithAction,
                             &setupMetroTimeSigNum);
cMenuItem melMetroTimeSigDenum(menuMetro,
                               "Time signature denum",
                               nullptr,
                               menuTypeItemListTextWithAction,
                               &setupMetroTimeSigDenum);
cMenuItem melMetroVolume(menuMetro, "Volume", nullptr, menuTypeItemListValues, &setupMetroVolume);

//==================================================================================================================================================================
// MIDI
const strItemTypeListText clockInSetup{ &mtConfig.midi.clkIn, 3, clockIn };
const strItemTypeListText clockOutSetup{ &mtConfig.midi.clkOut, 4, midiOut };
const strItemTypeListText transportInSetup{ &mtConfig.midi.transportIn, 3, clockIn };
const strItemTypeListText transportOutSetup{ &mtConfig.midi.transportOut, 4, midiOut };
const strItemTypeListText notesInModeSetup{ &mtConfig.midi.notesInMode, 4, midiIn };
const strItemTypeListText notesOutModeSetup{ &mtConfig.midi.notesOutMode, 4, midiOut };
const strItemTypeListText notesInChanneletup{ &mtConfig.midi.notesInChannel, 17, notesChannel };
const strItemTypeListText ccInModeSetup{ &mtConfig.midi.ccInMode, 4, midiIn };
const strItemTypeListText ccInChannelSetup{ &mtConfig.midi.ccInChannel, 17, notesChannel };
const strItemTypeListText middleCSetup{ &mtConfig.midi.middleC, 4, middleC };
const strItemTypeListTextWithAction slaveClockCompSetup{ &mtConfig.midi.slaveClockComp, std::size(slaveClockComp),  slaveClockComp, setSlaveClockCompValue };

cMenuItem melClockIn(menuMidi, "Clock in", nullptr, menuTypeItemListText, &clockInSetup);
cMenuItem melClockOut(menuMidi, "Clock out", nullptr, menuTypeItemListText, &clockOutSetup);
cMenuItem melTransportIn(menuMidi,
                         "Transport in",
                         nullptr,
                         menuTypeItemListText,
                         &transportInSetup);
cMenuItem melTransportOut(menuMidi,
                          "Transport out",
                          nullptr,
                          menuTypeItemListText,
                          &transportOutSetup);
cMenuItem melNotsInMode(menuMidi, "Notes in", nullptr, menuTypeItemListText, &notesInModeSetup);
cMenuItem melNotesInChannel(menuMidi,
                            "Notes in channel",
                            nullptr,
                            menuTypeItemListText,
                            &notesInChanneletup);
cMenuItem melNotsOutMode(menuMidi, "MIDI out", nullptr, menuTypeItemListText, &notesOutModeSetup);
cMenuItem melCCInMode(menuMidi, "CC in", supportedCcInDesc, menuTypeItemListText, &ccInModeSetup);
cMenuItem melCCInChannel(menuMidi,
                         "CC in channel",
                         supportedCcInDesc,
                         menuTypeItemListText,
                         &ccInChannelSetup);
cMenuItem melMiddleC(menuMidi, "Middle C", nullptr, menuTypeItemListText, &middleCSetup);
cMenuItem melSlaveClockComp(menuMidi,
                            "Clock sync correction",
                            slaveClockCompDesc,
                            menuTypeItemListTextWithAction,
                            &slaveClockCompSetup);

//==================================================================================================================================================================
// Firmware
const strItemTypeActionButton updateFirmwareSetup{ firmwareUpgradeActivate, nullptr };
const strItemTypeLabel currentVersionSetup{ interfaceGlobals.currentFirmwareVersion };
const strItemTypeLabelPtr currentBuildSetup{ &interfaceGlobals.currentFirmwareBuild };
const strItemTypeActionButton resetConfigSetup{ resetConfigShowPopup, nullptr };

cMenuItem melUpdateFirmware(menuFirmware,
                            "Firmware update",
                            firmUpdDesc,
                            menuTypeItemActionButton,
                            &updateFirmwareSetup);
cMenuItem melCurrentVersion(menuFirmware,
                            "Current version",
                            nullptr,
                            menuTypeItemLabel,
                            &currentVersionSetup);
cMenuItem melCurrentBuild(menuFirmware,
                            "Build",
                            nullptr,
                            menuTypeItemLabelPtr,
                            &currentBuildSetup);
cMenuItem melResetConfig(menuFirmware,
                         "Reset config",
                         nullptr,
                         menuTypeItemActionButton,
                         &resetConfigSetup);

//==================================================================================================================================================================
// USB

const strItemTypeActionButton usbStorageSetup{ usbStorageEnter, nullptr };
const strItemTypeListTextWithAction usbAudioSetup{ &mtConfig.common.usbAudioMode,
                                              sizeof(kUsbAudioList)/sizeof(kUsbAudioList[0]), kUsbAudioList, usbAudioChange };

cMenuItem melUsbStorage(menuUsb,
                          "Storage",
						  nullptr,
						  menuTypeItemActionButton,
                          &usbStorageSetup);

cMenuItem melUsbAudio(menuUsb,
                          "Audio",
						  usbAudioDesc,
						  menuTypeItemListTextWithAction,
                          &usbAudioSetup);
//==================================================================================================================================================================
// File transfer
// const strItemTypeActionButton startfileTransferSetup 		{ startFileTransferMode, nullptr};
// cMenuItem melStartFileTransfer	(menuFileTransfer, "Start file transfer",	nullptr,
// menuTypeItemActionButton, &startfileTransferSetup);

//==================================================================================================================================================================
// Credits & Info

const strItemTypeActionButton creditsSetup{ openCreditsAction, nullptr };
const strItemTypeActionButton manual1Setup{ openManual1Action, nullptr };
const strItemTypeActionButton manual2Setup{ openManual2Action, nullptr };
const strItemTypeActionButton manual3Setup{ openManual3Action, nullptr };

cMenuItem melOpenCredits(menuCredits,
                         "About",
                         nullptr,
                         menuTypeItemActionButton,
                         &creditsSetup);

cMenuItem melOpenManual1(menuCredits,
                         &manualFolderNames[0][0],
                         nullptr,
                         menuTypeItemActionButton,
                         &manual1Setup);

cMenuItem melOpenManual2(menuCredits,
                         &manualFolderNames[1][0],
                         nullptr,
                         menuTypeItemActionButton,
                         &manual2Setup);

cMenuItem melOpenManual3(menuCredits,
                         &manualFolderNames[2][0],
                         nullptr,
                         menuTypeItemActionButton,
                         &manual3Setup);


//==================================================================================================================================================================
// Function keys binding
#ifdef TRACKER_MINI

const strItemTypeListTextWithAction setupFunctionKeys1{ &mtConfig.interface.functionKeysBinding[0],
	                                          kMenuModulesCount,  kModuleNamesList, setFunctionKey1 };
const strItemTypeListTextWithAction setupFunctionKeys2{ &mtConfig.interface.functionKeysBinding[1],
                                              kMenuModulesCount,  kModuleNamesList, setFunctionKey2 };
const strItemTypeListTextWithAction setupFunctionKeys3{ &mtConfig.interface.functionKeysBinding[2],
                                              kMenuModulesCount, kModuleNamesList, setFunctionKey3 };
const strItemTypeListTextWithAction setupFunctionKeys4{ &mtConfig.interface.functionKeysBinding[3],
	                                          kMenuModulesCount,  kModuleNamesList, setFunctionKey4 };
const strItemTypeListTextWithAction setupFunctionKeys5{ &mtConfig.interface.functionKeysBinding[4],
	                                          kMenuModulesCount, kModuleNamesList, setFunctionKey5 };
const strItemTypeListTextWithAction setupFunctionKeys6{ &mtConfig.interface.functionKeysBinding[5],
                                              kMenuModulesCount, kModuleNamesList, setFunctionKey6 };
const strItemTypeListTextWithAction setupFunctionKeys7{ &mtConfig.interface.functionKeysBinding[6],
                                              kMenuModulesCount, kModuleNamesList, setFunctionKey7 };
const strItemTypeListTextWithAction setupFunctionKeys8{ &mtConfig.interface.functionKeysBinding[7],
                                              kMenuModulesCount, kModuleNamesList, setFunctionKey8 };

cMenuItem melFunctionButtons1(menuFunctionButtons,
                              "Fn1",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys1);
cMenuItem melFunctionButtons2(menuFunctionButtons,
                              "Fn2",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys2);
cMenuItem melFunctionButtons3(menuFunctionButtons,
                              "Fn3",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys3);
cMenuItem melFunctionButtons4(menuFunctionButtons,
                              "Fn4",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys4);
cMenuItem melFunctionButtons5(menuFunctionButtons,
                              "Fn1+Shift",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys5);
cMenuItem melFunctionButtons6(menuFunctionButtons,
                              "Fn2+Shift",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys6);
cMenuItem melFunctionButtons7(menuFunctionButtons,
                              "Fn3+Shift",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys7);
cMenuItem melFunctionButtons8(menuFunctionButtons,
                              "Fn4+Shift",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys8);

#endif

// clang-format on

//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================

hMenuItem activeSubmenu;

void cConfigEditor::createConfigMenu()
{
    activeSubmenu = menuBase.getSelChild();

    createMenuBaseList();
    refreshConfigMenu();
}

void cConfigEditor::hideConfigMenu()
{

    display.setControlHide(configLabel);

    display.setControlHide(configListControl);
    display.setControlHide(configBasemenuListControl);
    display.setControlHide(configSubmenuListControl);
    display.setControlHide(frameControl);
    display.setControlHide(blankSpaceCover[0]);
    display.setControlHide(blankSpaceCover[1]);

    display.refreshControl(frameControl);
}

void cConfigEditor::createMenuBaseList()
{
    basemenuList.linesCount = 14;
    basemenuList.start = menuBase.getSelectedItem();
    basemenuList.length = menuBase.getCount();
    basemenuList.data = menuBase.getNames();

    display.setControlData(configBasemenuListControl, &basemenuList);
    display.refreshControl(configBasemenuListControl);
    display.setControlShow(configBasemenuListControl);
}

void cConfigEditor::reloadSubmenu()
{
    cMenuGroup* submenu = (cMenuGroup*)activeSubmenu;

    submenuList.linesCount = 14;
    submenuList.start = submenu->getSelectedItem();
    submenuList.length = submenu->getCount();
    submenuList.params = submenu->getNames();
    submenuList.values = submenu->getValues();
    submenuList.valueEditActive = 0;

    if (submenu->parentMenu != &menuBase)
        changeLabelText(6, "Back");
    else
        changeLabelText(6, "");

    display.setControlData(configSubmenuListControl, &submenuList);
    display.refreshControl(configSubmenuListControl);
    display.setControlShow(configSubmenuListControl);
}

void cConfigEditor::loadConfigTextList(strItemTypeListText* itemSetup)
{
    changeLabelText(7, "Apply");
    changeLabelText(6, "Cancel");

    showConfigList4(2, *itemSetup->value, itemSetup->count, (char**)itemSetup->ptrText);
}

void cConfigEditor::loadConfigValuesList(strItemTypeListValues* itemSetup)
{
    changeLabelText(7, "Apply");
    changeLabelText(6, "Cancel");

    showConfigList4Val(
      itemSetup->value, itemSetup->min, itemSetup->max, itemSetup->interval, itemSetup->dev);
}

// wyjdz do menu wyzszego poziomu
bool cConfigEditor::menuGoOut()
{
    // tylko jesli nadrzede menu jest inne niz bazowe
    if (((cMenuGroup*)activeSubmenu)->parentMenu != &menuBase)
    {
        activeSubmenu = ((cMenuGroup*)activeSubmenu)->parentMenu;

        refreshConfigMenu();

        return true;
    }

    return false;
}

// wejdz do nizszego podmenu
void cConfigEditor::menuGoIn()
{
    // wykonaj akcje na zaznaczonej pozycji wybranej listy
    if (((cMenuGroup*)activeSubmenu)->type == menuTypeGroup)
    {
        // jesli to kolejna grupa to ustaw ja jako aktywna i odswiez
        if (((cMenuGroup*)activeSubmenu)->getSelChild()->type == menuTypeGroup)
        {
            activeSubmenu = ((cMenuGroup*)activeSubmenu)->getSelChild();

            refreshConfigMenu();
        }
        // jesli to element ostateczny przejdz do jego edycji/akcji
        else if (((cMenuGroup*)activeSubmenu)->getSelChild()->type == menuTypeItem)
        {
            ((cMenuGroup*)activeSubmenu)->executeItem();
        }
    }
}

// source = 0 - encoder, = 1 - klawisze pod ekranem
void cConfigEditor::changeMenuListPosition(uint8_t list, int16_t value, uint8_t)
{
    // sprawdzanie czy taka lista wogole dostepna
    if (list == 1 && menuBase.getSelChild()->type != menuTypeGroup)
        return;

    // wykownywanie akcji na zjechanie/ukrycie itemu listy ActionButton
    uint8_t itemPositionChanged = 0;
    void (*action_funct)(void) = nullptr;
    cMenuItem* selectedMenuItem = nullptr;

    if (itemEditorShown && list == 0)
    {
        itemEditorClose();
    }

    if (list == 0)
    {
        uint8_t start_position = menuBase.selectedItem;

        if (menuBase.selectedItem + value < 0)
            menuBase.selectedItem = 0;
        else if (menuBase.selectedItem + value > menuBase.getCount() - 1)
            menuBase.selectedItem = menuBase.getCount() - 1;
        else
            menuBase.selectedItem += value;

        if (start_position != menuBase.selectedItem)
            itemPositionChanged++;

        activeSubmenu = menuBase.getSelChild();

        display.setControlValue(configBasemenuListControl, menuBase.selectedItem);
        display.refreshControl(configBasemenuListControl);
    }
    else if (list == 1)
    {
        cMenuGroup* submenu = (cMenuGroup*)activeSubmenu;

        uint8_t start_position = submenu->selectedItem;

        uint8_t* item_selected = &submenu->selectedItem;
        uint8_t item_max = submenu->getCount() - 1;

        if (*item_selected + value < 0)
            *item_selected = 0;
        else if (*item_selected + value > item_max)
            *item_selected = item_max;
        else
            *item_selected += value;

        display.setControlValue(configSubmenuListControl, submenu->selectedItem);
        display.refreshControl(configSubmenuListControl);

        if (start_position != submenu->selectedItem)
            itemPositionChanged++;
    }

    if (itemPositionChanged)
    {
        // odswiezenie menu
        if (selectedMenuItem != nullptr)
            selectedMenuItem->resetValueEditState();

        refreshConfigMenu();

        // sprawdzanie czy zmieniono element menu z wczesniejszego typu actionbutton
        if (action_funct != nullptr)
        {
            action_funct();
        }
    }
}

void cConfigEditor::chanegeItemValue(void* selectedMenuItem, int16_t value)
{
    if (selectedMenuItem == nullptr)
        return;
    if (((cMenuItem*)selectedMenuItem)->getItemType() != menuItemTypeValueU8)
        return; // narazie tylko dla U8

    strItemTypeValue8* itemSetup =
      (strItemTypeValue8*)((cMenuItem*)selectedMenuItem)->getItemSetup();

    if (*itemSetup->value + value < itemSetup->min)
        *itemSetup->value = itemSetup->min;
    else if (*itemSetup->value + value > itemSetup->max)
        *itemSetup->value = itemSetup->max;
    else
        *itemSetup->value += value;
}

void cConfigEditor::refreshConfigMenu()
{
    reloadConfigLabels();

    assert(menuBase.getSelectedItem() < menuBase.getCount());
    changeLabelText(0, menuBase.getNames()[menuBase.getSelectedItem()]);

    if (menuBase.getSelChild()->type == menuTypeGroup)
    {
        assert(menuBase.getSelChild()->getSelectedItem() < menuBase.getSelChild()->getCount());
        changeLabelText(
          3, menuBase.getSelChild()->getNames()[menuBase.getSelChild()->getSelectedItem()]);
    }

    if (menuBase.type == menuTypeItem)
    {
        hideSubmenu();
        return;
    }
    else if (menuBase.type == menuTypeGroup)
    {
        reloadSubmenu();
        showSubmenu();

        // ustawianie etykiety przycisku edycji/akcji
        if (((cMenuGroup*)activeSubmenu)->getSelChild()->type == menuTypeItem)
        {
            cMenuItem* selected_child = (cMenuItem*)((cMenuGroup*)activeSubmenu)->getSelChild();

            setLabelByMenuItemType(
              7, selected_child->getItemType(), selected_child->getValueEditState());
        }
        else
        {
            changeLabelText(7, "Enter"); // xxx wejscie do podmenu
        }

        refreshListsColors();
    }

    display.synchronizeRefresh();
}

void cConfigEditor::setLabelByMenuItemType(uint8_t label, menu_item_t type, uint8_t variant)
{
    switch (type)
    {
        case menuItemTypeValueU8:
        {
            if (variant == 0)
                changeLabelText(label, "Enter");
            else if (variant == 1)
                changeLabelText(label, "Confirm");
        }
        break;
        case menuTypeItemListValues: changeLabelText(label, "Enter"); break;
        case menuTypeItemListText: changeLabelText(label, "Enter"); break;
        case menuTypeItemListTextWithAction: changeLabelText(label, "Enter"); break;
        case menuTypeItemLabel: changeLabelText(label, ""); break;
        case menuTypeItemLabelPtr: changeLabelText(label, ""); break;
        case menuItemTypeEmpty: changeLabelText(label, ""); break;
        case menuTypeItemActionButton: changeLabelText(label, "Enter"); break;
        default: changeLabelText(label, ""); break;
    }
}

// edytowanie wartosci elementu menu
void cConfigEditor::itemEditorOpen(menu_item_t type,
                                   const char* title,
                                   const char* description,
                                   const void* setup)
{
    hideSubmenu();

    itemEditorShown = 1;

    showConfigItemLabel(title);
    showConfigItemDesctription(description);

    switch (type)
    {
        case menuItemTypeValueU8:
        {
            configEditor.loadConfigValuesList((strItemTypeListValues*)setup);
            break;
        }
        case menuTypeItemListValues:
        {
            configEditor.loadConfigValuesList((strItemTypeListValues*)setup);
            break;
        }
        case menuTypeItemListText:
        {
            configEditor.loadConfigTextList((strItemTypeListText*)setup);
            break;
        }
        case menuTypeItemListTextWithAction:
        {
            if (setup == &setupDispTheme || setup == &setupDispFont)
            {
                ThemeManager::scanThemesAndFonts();
                refreshThemeAndFontMenuOptions();
            }
            configEditor.loadConfigTextList((strItemTypeListText*)setup);
            break;
        }
        case menuTypeItemActionButton:
        {

            break;
        }

        default: break;
    }
}

void cConfigEditor::itemEditorClose()
{
    itemEditorShown = 0;
    hideConfigList();
    hideConfigItemLabel();
    hideConfigItemDesctription();

    refreshConfigMenu();

    cMenuItem* selected_child = (cMenuItem*)((cMenuGroup*)activeSubmenu)->getSelChild();
    switch (selected_child->getItemType())
    {
        case menuTypeItemActionButton:
        {
            if (((strItemTypeActionButton*)selected_child->getItemSetup())->funct2 != nullptr)
            {
                ((strItemTypeActionButton*)selected_child->getItemSetup())->funct2();
            }
            break;
        }

        default: break;
    }
}

void cConfigEditor::itemEditorApply()
{
    configListConfirm(configEditor.selectedConfigListPosition);

    itemEditorClose();
}

void cConfigEditor::itemEditorChangeValue(int16_t value)
{
    changeConfigListPosition(value);
}

void cConfigEditor::configListConfirm(uint8_t list_pos)
{
    if (((cMenuGroup*)activeSubmenu)->getSelChild()->type != menuTypeItem)
    {
        return;
    }

    cMenuItem* selected_child = (cMenuItem*)((cMenuGroup*)activeSubmenu)->getSelChild();

    switch (selected_child->getItemType())
    {
        case menuItemTypeValueU8:
        {

            break;
        }
        case menuTypeItemListValues:
        {
            strItemTypeListValues* temp_str =
              (strItemTypeListValues*)selected_child->getItemSetup();
            if (list_pos <= temp_str->max - temp_str->min)
                *(temp_str->value) = temp_str->min + list_pos;
            break;
        }
        case menuTypeItemListText:
        {
            strItemTypeListText* temp_str = (strItemTypeListText*)selected_child->getItemSetup();
            if (list_pos < temp_str->count)
                *(temp_str->value) = list_pos;
            break;
        }
        case menuTypeItemListTextWithAction:
        {
            strItemTypeListTextWithAction* temp_str =
              (strItemTypeListTextWithAction*)selected_child->getItemSetup();
            if (list_pos < temp_str->count)
                *(temp_str->value) = list_pos;
            temp_str->funct1();
            break;
        }
        case menuTypeItemActionButton:
        {

            break;
        }
        default: break;
    }

    saveConfigToEeprom();
}

// wykonywanie akcji/edycji na elemencie menu
void cMenuGroup::executeItem()
{
    if (childs[selectedItem] == nullptr || childs[selectedItem]->type != menuTypeItem)
        return;

    cMenuItem* selected_child = (cMenuItem*)childs[selectedItem];

    switch (selected_child->itemType)
    {
        case menuItemTypeValueU8:
        case menuTypeItemListValues:
        case menuTypeItemListText:
        case menuTypeItemListTextWithAction:
            configEditor.itemEditorOpen(selected_child->itemType,
                                        selected_child->itemName,
                                        selected_child->itemDesc,
                                        selected_child->itemSetup);
            break;
        case menuTypeItemLabel: break;
        case menuTypeItemLabelPtr: break;
        case menuItemTypeEmpty: break;
        case menuTypeItemActionButton:
        {
            if (((strItemTypeActionButton*)selected_child->itemSetup)->funct1 != nullptr)
            {
                ((strItemTypeActionButton*)selected_child->itemSetup)->funct1();
            }

            break;
        }

        default: break;
    }
}

void cMenuGroup::reloadValues()
{
    for (uint8_t i = 0; i < getCount(); i++)
    {
        if (childs[i] != nullptr && childs[i]->type == menuTypeItem)
        {
            char* valueText = ((cMenuItem*)childs[i])->getValue();

            childsValues[i] = valueText != nullptr ? valueText : (char*)ptrEmptyName;
        }
        else
        {
            childsValues[i] = (char*)ptrEmptyName;
        }
    }
}
