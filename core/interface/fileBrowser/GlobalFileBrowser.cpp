#include "GlobalFileBrowser.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "ampLogarythmicValues.h"
#include "config.h"
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "core/titleBar.h"
#include "display.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "util.h"
#include "util_math.h"
#include "util_string.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cr_section_macros.h>
#include <cstring>

extern uint8_t externalRamScratchBuff[];

cGlobalFileBrowser globalFileBrowser;

__NOINIT(EXTERNAL_RAM) static char s_displayLabelsBuffer[kMaxFilesListLength][32];
__NOINIT(EXTERNAL_RAM) static char* s_displayListPtrs[kMaxFilesListLength];

static const uint16_t s_gfbFramesPlaces[3][4] = {
    { 0 + 1, 31, (800 / 8) * 3 - 3, 391 },
    { (800 / 8) * 3 + 1, 31, (800 / 8) * 3 - 3, 391 },
    { (800 / 8) * 6 + 1, 31, (800 / 8) * 1 - 3, 391 },
};

// Use top 24 KB of externalRamScratchBuff ([744 KB .. 768 KB]) so sdStream ([0 .. 320 KB])
// and .ptfx plugin code/scratch ([0 .. 744 KB]) are never overwritten.
static constexpr uint32_t kBrowserScratchBytes = 24 * 1024;
static constexpr uint32_t kBrowserScratchOffset =
  SHARED_SCRATCH_MEMORY_SIZE - kBrowserScratchBytes;
static constexpr int kMaxSecondaryListItems = 32;

static constexpr uint32_t kNamesBufferBytes = kMaxFilesListLength * kMaxFileNameLen;
static constexpr uint32_t kBrowserListBytes = kMaxFilesListLength * sizeof(char*);
static constexpr uint32_t kSelectionListBytes = kMaxFilesListLength * sizeof(char);
static constexpr uint32_t kSecondaryPtrsBytes = kMaxSecondaryListItems * sizeof(char*);

static_assert(kNamesBufferBytes + kBrowserListBytes + kSelectionListBytes + kSecondaryPtrsBytes <=
                kBrowserScratchBytes,
              "GlobalFileBrowser scratch exceeds 24 KB");

namespace
{
struct SavedContextEntry
{
    char key[48];
    char path[kMaxFilePathLen + 1];
    int position;
    uint32_t stamp;
    bool valid;
};

static constexpr int kMaxSavedContexts = 10;
SavedContextEntry s_savedContexts[kMaxSavedContexts]{};
uint32_t s_contextStampCounter = 1;

bool isProtectedRootFolder(const char* folderName)
{
    if (folderName == nullptr || folderName[0] == '\0')
    {
        return true;
    }

    static const char* const kProtectedFolders[] = {
        "Samples",    "SynthSamples", "Synths",    "Patches",   "Projects",
        "Mod",        "Mods",         "Effects",   "Recordings","Export",
        "Workspace",  "Firmware",     "Themes",    "Games",     "System Volume Information",
    };

    for (const char* protectedName : kProtectedFolders)
    {
        if (strcasecmp(folderName, protectedName) == 0)
        {
            return true;
        }
    }
    return false;
}

void formatFileSize(size_t bytes, char* out, size_t outSize)
{
    if (bytes < 1024)
    {
        snprintf(out, outSize, "%u B", static_cast<unsigned>(bytes));
    }
    else if (bytes < 1024u * 1024u)
    {
        uint32_t tenths = static_cast<uint32_t>((bytes * 10u) / 1024u);
        snprintf(out,
                 outSize,
                 "%u.%u KB",
                 static_cast<unsigned>(tenths / 10u),
                 static_cast<unsigned>(tenths % 10u));
    }
    else
    {
        uint32_t hundredths = static_cast<uint32_t>((bytes * 100u) / (1024u * 1024u));
        snprintf(out,
                 outSize,
                 "%u.%02u MB",
                 static_cast<unsigned>(hundredths / 100u),
                 static_cast<unsigned>(hundredths % 100u));
    }
}

bool readWavMetadata(FileSystem* fs,
                     const char* fullPath,
                     uint32_t& outSampleRate,
                     uint16_t& outChannels,
                     uint16_t& outBitsPerSample,
                     uint32_t& outDataBytes)
{
    outSampleRate = 0;
    outChannels = 0;
    outBitsPerSample = 0;
    outDataBytes = 0;

    if (fs == nullptr || fullPath == nullptr)
    {
        return false;
    }

    auto [isOpen, fh] = fs->openFile(fullPath, FileSystem::FileOpenMode::kRead);
    if (!isOpen)
    {
        return false;
    }

    uint8_t riffHdr[12]{};
    if (fs->readFile(riffHdr, sizeof(riffHdr), fh) != sizeof(riffHdr) ||
        memcmp(riffHdr, "RIFF", 4) != 0 || memcmp(riffHdr + 8, "WAVE", 4) != 0)
    {
        fs->closeFile(fh);
        return false;
    }

    bool foundFmt = false;
    bool foundData = false;
    for (int i = 0; i < 16 && (!foundFmt || !foundData); ++i)
    {
        uint8_t chunkHdr[8]{};
        if (fs->readFile(chunkHdr, sizeof(chunkHdr), fh) != sizeof(chunkHdr))
        {
            break;
        }
        uint32_t chunkSize = 0;
        memcpy(&chunkSize, chunkHdr + 4, 4);

        if (memcmp(chunkHdr, "fmt ", 4) == 0 && chunkSize >= 16)
        {
            uint8_t fmtBuf[16]{};
            if (fs->readFile(fmtBuf, sizeof(fmtBuf), fh) != sizeof(fmtBuf))
            {
                break;
            }
            memcpy(&outChannels, fmtBuf + 2, 2);
            memcpy(&outSampleRate, fmtBuf + 4, 4);
            memcpy(&outBitsPerSample, fmtBuf + 14, 2);
            foundFmt = true;

            uint32_t rem = ((chunkSize + 1u) & ~1u) - 16u;
            if (rem > 0 && !fs->seekFileCur(static_cast<int>(rem), fh))
            {
                break;
            }
        }
        else if (memcmp(chunkHdr, "data", 4) == 0)
        {
            outDataBytes = chunkSize;
            foundData = true;
            break;
        }
        else
        {
            if (chunkSize > 0x7FFFFFFEu)
            {
                break;
            }
            uint32_t padded = (chunkSize + 1u) & ~1u;
            if (padded > 0 && !fs->seekFileCur(static_cast<int>(padded), fh))
            {
                break;
            }
        }
    }

    fs->closeFile(fh);
    return foundFmt && outSampleRate > 0 && outChannels > 0 && outBitsPerSample > 0;
}

void ensureDirRecursive(FileSystem* fs, const std::string& relPath)
{
    if (fs == nullptr || relPath.empty())
    {
        return;
    }

    std::string acc;
    size_t pos = 0;
    while (pos < relPath.size())
    {
        size_t nextSlash = relPath.find('/', pos);
        std::string segment = (nextSlash == std::string::npos)
                                ? relPath.substr(pos)
                                : relPath.substr(pos, nextSlash - pos);
        if (!segment.empty())
        {
            if (!acc.empty())
            {
                acc += '/';
            }
            acc += segment;
            if (!fs->exists(acc.c_str()))
            {
                fs->mkdir(0, acc.c_str());
            }
        }
        if (nextSlash == std::string::npos)
        {
            break;
        }
        pos = nextSlash + 1;
    }
}
} // namespace

void openGlobalFileBrowser(const GlobalFileBrowserConfig& config)
{
    globalFileBrowser.configure(config);
    globalFileBrowser.savedPrevModule = mtInterface.getPreviousModule();
    globalFileBrowser.savedPrevModuleOptions = mtInterface.getPreviousModuleOptions();
    if (config.returnModule != nullptr && config.returnModule != &globalFileBrowser)
    {
        mtInterface.deactivateModule(config.returnModule);
    }
    mtInterface.activateModule(&globalFileBrowser, 0);
}

static void gfbEncoderTurn(int16_t dir)
{
    if (globalFileBrowser.dialogState != cGlobalFileBrowser::DialogState::kBrowse ||
        globalFileBrowser.keyboardManager.getState())
    {
        return;
    }
    globalFileBrowser.handleEncoderOrArrows(dir, true);
}

static void gfbArrowUp()
{
    if (globalFileBrowser.dialogState != cGlobalFileBrowser::DialogState::kBrowse)
    {
        return;
    }
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeMove('w');
        return;
    }
    globalFileBrowser.handleEncoderOrArrows(1, false);
}

static void gfbArrowDown()
{
    if (globalFileBrowser.dialogState != cGlobalFileBrowser::DialogState::kBrowse)
    {
        return;
    }
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeMove('s');
        return;
    }
    globalFileBrowser.handleEncoderOrArrows(-1, false);
}

static void gfbArrowLeft()
{
    if (globalFileBrowser.dialogState != cGlobalFileBrowser::DialogState::kBrowse)
    {
        return;
    }
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeMove('a');
        return;
    }
    globalFileBrowser.handleLeftRight(-1);
}

static void gfbArrowRight()
{
    if (globalFileBrowser.dialogState != cGlobalFileBrowser::DialogState::kBrowse)
    {
        return;
    }
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeMove('d');
        return;
    }
    globalFileBrowser.handleLeftRight(1);
}

static void gfbEnterButton()
{
    if (globalFileBrowser.dialogState != cGlobalFileBrowser::DialogState::kBrowse)
    {
        globalFileBrowser.confirmDialogAction();
        return;
    }
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.confirmKey();
        return;
    }
    globalFileBrowser.handleEnterOrInsert();
}

static void gfbDeleteButton()
{
    if (globalFileBrowser.dialogState != cGlobalFileBrowser::DialogState::kBrowse)
    {
        return;
    }
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeBackspace();
        return;
    }
    if (globalFileBrowser.focusedPane == cGlobalFileBrowser::FocusPane::kPreviewVolume)
    {
        mtConfig.general.previewVolume = PREVIEW_VOLUME_DEFAULT;
        engine.setPreviewVolume(mtConfig.general.previewVolume);
        saveConfig();
        globalFileBrowser.refreshBrowserScreen();
        return;
    }
    if (globalFileBrowser.focusedPane == cGlobalFileBrowser::FocusPane::kExplorer)
    {
        globalFileBrowser.handleDeleteRequest();
    }
}

static void gfbPadChange(uint8_t pad, uint8_t state, int16_t velo)
{
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.onPadChange(pad, state);
        return;
    }
    globalFileBrowser.handlePadInput(pad, state, velo);
}

static void gfbScreenButton(uint8_t button, uint8_t state)
{
    if (globalFileBrowser.dialogState != cGlobalFileBrowser::DialogState::kBrowse)
    {
        if (state == buttonPress)
        {
            if (button == interfaceButton6)
            {
                globalFileBrowser.cancelDialogAction();
            }
            else if (button == interfaceButton7)
            {
                globalFileBrowser.confirmDialogAction();
            }
        }
        return;
    }

    if (globalFileBrowser.keyboardManager.getState())
    {
        if (state == buttonPress)
        {
            globalFileBrowser.handleKeyboardButtonPress(button);
        }
        return;
    }

    if (state == buttonPress)
    {
        globalFileBrowser.handleActionButtonPress(button);
    }
    else if (state == buttonRelease)
    {
        globalFileBrowser.handleActionButtonRelease(button);
    }
}

static void gfbBtn0(uint8_t state) { gfbScreenButton(interfaceButton0, state); }
static void gfbBtn1(uint8_t state) { gfbScreenButton(interfaceButton1, state); }
static void gfbBtn2(uint8_t state) { gfbScreenButton(interfaceButton2, state); }
static void gfbBtn3(uint8_t state) { gfbScreenButton(interfaceButton3, state); }
static void gfbBtn4(uint8_t state) { gfbScreenButton(interfaceButton4, state); }
static void gfbBtn5(uint8_t state) { gfbScreenButton(interfaceButton5, state); }
static void gfbBtn6(uint8_t state) { gfbScreenButton(interfaceButton6, state); }
static void gfbBtn7(uint8_t state) { gfbScreenButton(interfaceButton7, state); }

static void gfbSwitchModule(uint8_t button)
{
    globalFileBrowser.keyboardPurpose = cGlobalFileBrowser::KeyboardPurpose::kNone;
    globalFileBrowser.dialogState = cGlobalFileBrowser::DialogState::kBrowse;
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.deactivateKeyboard();
    }
    mtPopups.hideInfoPopup();

    globalFileBrowser.cancelBrowser();
    if (globalFileBrowser.ip != nullptr)
    {
        globalFileBrowser.ip->processButtonsInput(button, buttonPress);
    }
}

static void gfbMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        mtMenuPopup.showModulePopup(&globalFileBrowser, gfbSwitchModule);
    }
    else if (state == buttonRelease)
    {
        mtMenuPopup.hideModulePopup(&globalFileBrowser);
    }
}

void cGlobalFileBrowser::configure(const GlobalFileBrowserConfig& cfg)
{
    config = cfg;
}

bool cGlobalFileBrowser::isSaveMode() const
{
    return config.mode == GlobalFileBrowserConfig::Mode::kSaveWav ||
           config.mode == GlobalFileBrowserConfig::Mode::kSaveGenericFile ||
           config.mode == GlobalFileBrowserConfig::Mode::kSaveFolderPackage;
}

bool cGlobalFileBrowser::isPackageMode() const
{
    return config.mode == GlobalFileBrowserConfig::Mode::kLoadFolderPackage ||
           config.mode == GlobalFileBrowserConfig::Mode::kSaveFolderPackage;
}

bool cGlobalFileBrowser::hasPreviewVolumePane() const
{
    return config.mode == GlobalFileBrowserConfig::Mode::kLoadWav ||
           config.mode == GlobalFileBrowserConfig::Mode::kSaveWav;
}

bool cGlobalFileBrowser::hasSecondaryListPane() const
{
    return !config.secondaryListItems.empty();
}

void cGlobalFileBrowser::initDisplayControls()
{
    strControlProperties prop{};
    frameData.placesCount = 1;
    frameData.startPlace = 0;
    frameData.places[0] = &s_gfbFramesPlaces[0][0];
    prop.style = 0;
    prop.value = 0;
    prop.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop);

    strControlProperties prop2{};
    prop2.value = 1;
    prop2.colors = display.getTheme().activeLabelsColors;
    prop2.style = (controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
    prop2.y = 424;
    prop2.h = 55;

    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.x = (800 / 8) * i + (800 / 16);
        prop2.w = 800 / 8 - 6;
        label[i] = display.createControl<cLabel>(&prop2);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
    }

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
    prop2.colors = display.getTheme().topBarLabelColors;
    prop2.style = (controlStyleShow | controlStyleBackground | controlStyleCenterY |
                   controlStyleCenterX | controlStyleFont4);
    prop2.x = (800 / 16) * 3 - 1;
    prop2.w = (800 / 8) * 3 - 3;
    prop2.y = 32;
    prop2.h = 26;
    explorerLabelControl = display.createControl<cLabel>(&prop2);

    explorerList.start = 0;
    explorerList.length = 0;
    explorerList.linesCount = 13;
    explorerList.data = nullptr;
    explorerList.lineWidthMax = (800 / 8) * 3 - 30;
    explorerList.lineTextLenMax = 32;
    prop.x = 0 + 1;
    prop.y = 29 + 32;
    prop.w = (800 / 8) * 3 - 3;
    prop.h = 394 - 32;
    prop.style = controlStyleBackground |
                 (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop.data = &explorerList;
    explorerListControl = display.createControl<cList>(&prop);

    strControlProperties coverProp{};
    coverProp.value = 0;
    coverProp.data = nullptr;
    coverProp.text = nullptr;
    coverProp.colors = display.getTheme().blankSpaceCoverLabelColors;
    coverProp.style = controlStyleBackground |
                      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    coverProp.x = (800 / 8) * 3 + 1;
    coverProp.w = (800 / 8) * 3 - 3;
    coverProp.y = 29;
    coverProp.h = 394;
    middleCoverControl = display.createControl<cLabel>(&coverProp);

    prop2.value = 0;
    prop2.colors = display.getTheme().topBarLabelColors;
    prop2.style = (controlStyleShow | controlStyleBackground | controlStyleCenterY |
                   controlStyleCenterX | controlStyleFont4);
    prop2.x = (800 / 16) * 9 - 1;
    prop2.w = (800 / 8) * 3 - 3;
    prop2.y = 32;
    prop2.h = 26;
    middleHeaderControl = display.createControl<cLabel>(&prop2);

    secondaryList.start = 0;
    secondaryList.length = 0;
    secondaryList.linesCount = 13;
    secondaryList.data = nullptr;
    secondaryList.lineWidthMax = (800 / 8) * 3 - 30;
    secondaryList.lineTextLenMax = 32;
    prop.x = (800 / 8) * 3 + 1;
    prop.y = 29 + 32;
    prop.w = (800 / 8) * 3 - 3;
    prop.h = 394 - 32;
    prop.style = controlStyleBackground |
                 (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop.data = &secondaryList;
    secondaryListControl = display.createControl<cList>(&prop);

    for (uint8_t i = 0; i < 4; i++)
    {
        strControlProperties inspProp{};
        inspProp.x = (800 / 16) * 9;
        inspProp.w = (800 / 8) * 3 - 20;
        inspProp.y = 120 + i * 44;
        inspProp.h = 32;
        inspProp.value = 0;
        inspProp.text = inspectorText[i];
        inspProp.colors = (i == 0) ? display.getTheme().activeButtonLabelsColors
                                   : display.getTheme().inactiveButtonLabelsColors;
        inspProp.style = controlStyleCenterX | controlStyleCenterY |
                         (i == 0 ? controlStyleFont3 : controlStyleFont2);
        inspectorLabel[i] = display.createControl<cLabel>(&inspProp);
    }

    strControlProperties previewVolumeProperties{};
    previewVolumeProperties.x = (800 / 8) * 6 + 1;
    previewVolumeProperties.w = 800 / 8 - 3;
    previewVolumeProperties.y = 29;
    previewVolumeProperties.h = 394;
    previewVolumeProperties.style =
      controlStyleValue_0_100 | controlStyleBackground |
      (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    previewVolumeBarControl = display.createControl<cBar>(&previewVolumeProperties);

    coverProp.x = (800 / 8) * 7 + 1;
    coverProp.w = (800 / 8) * 1 - 2;
    rightCoverControl = display.createControl<cLabel>(&coverProp);
}

std::string cGlobalFileBrowser::buildEffectiveContextKey() const
{
    if (!config.contextKey.empty())
    {
        return config.contextKey.substr(0, sizeof(SavedContextEntry::key) - 1);
    }
    char buf[48]{};
    snprintf(buf,
             sizeof(buf),
             "%d:%s:%s",
             static_cast<int>(config.mode),
             config.initialDirectory.c_str(),
             config.extensionFilter.c_str());
    return std::string(buf);
}

void cGlobalFileBrowser::saveCurrentContext() const
{
    if (!config.rememberLastPath)
    {
        return;
    }
    std::string effKey = buildEffectiveContextKey();
    int matchIdx = -1;
    int emptyIdx = -1;
    int oldestIdx = 0;
    uint32_t oldestStamp = 0xFFFFFFFFu;

    for (int i = 0; i < kMaxSavedContexts; ++i)
    {
        if (s_savedContexts[i].valid)
        {
            if (strcmp(s_savedContexts[i].key, effKey.c_str()) == 0)
            {
                matchIdx = i;
                break;
            }
            if (s_savedContexts[i].stamp < oldestStamp)
            {
                oldestStamp = s_savedContexts[i].stamp;
                oldestIdx = i;
            }
        }
        else if (emptyIdx < 0)
        {
            emptyIdx = i;
        }
    }

    int targetIdx = (matchIdx >= 0) ? matchIdx : (emptyIdx >= 0 ? emptyIdx : oldestIdx);
    polystrncpy(s_savedContexts[targetIdx].key, effKey.c_str(), sizeof(s_savedContexts[targetIdx].key));
    polystrncpy(s_savedContexts[targetIdx].path,
                fileBrowserData.currentPath,
                sizeof(s_savedContexts[targetIdx].path));
    s_savedContexts[targetIdx].position = fileBrowserData.browserListPosition;
    s_savedContexts[targetIdx].stamp = ++s_contextStampCounter;
    s_savedContexts[targetIdx].valid = true;
}

void cGlobalFileBrowser::rebuildFocusLayout()
{
    uint8_t count = 0;
    frameData.places[count++] = &s_gfbFramesPlaces[0][0];

    bool secPane = hasSecondaryListPane();
    bool volPane = hasPreviewVolumePane();

    if (secPane)
    {
        frameData.places[count++] = &s_gfbFramesPlaces[1][0];
    }
    if (volPane)
    {
        frameData.places[count++] = &s_gfbFramesPlaces[2][0];
    }
    frameData.placesCount = count;

    if (focusedPane == FocusPane::kSecondaryList && !secPane)
    {
        focusedPane = FocusPane::kExplorer;
    }
    if (focusedPane == FocusPane::kPreviewVolume && !volPane)
    {
        focusedPane = FocusPane::kExplorer;
    }

    if (focusedPane == FocusPane::kExplorer)
    {
        selectedPlace = 0;
    }
    else if (focusedPane == FocusPane::kSecondaryList)
    {
        selectedPlace = 1;
    }
    else
    {
        selectedPlace = secPane ? 2 : 1;
    }
}

void cGlobalFileBrowser::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;

    focusedPane = FocusPane::kExplorer;
    selectedPlace = 0;
    dialogState = DialogState::kBrowse;
    keyboardPurpose = KeyboardPurpose::kNone;
    pendingSavePath.clear();
    pendingEnteredName[0] = '\0';

    uint8_t* scratchBase = externalRamScratchBuff + kBrowserScratchOffset;
    memset(&fileBrowserData, 0, sizeof(fileBrowserData));
    fileBrowserData.mode = isPackageMode() ? kProjectBrowser : kSamplesBrowser;
    fileBrowserData.selectionEnabled = true;
    fileBrowserData.maxFileNameLen = kMaxFileNameLen;
    fileBrowserData.maxBrowserListLen = kMaxFilesListLength;
    fileBrowserData.fileNamesBufferSize = kNamesBufferBytes;
    fileBrowserData.fileNamesBuffer = reinterpret_cast<char*>(scratchBase);
    fileBrowserData.browserList = reinterpret_cast<char**>(scratchBase + kNamesBufferBytes);
    fileBrowserData.selectionList =
      reinterpret_cast<char*>(scratchBase + kNamesBufferBytes + kBrowserListBytes);

    polystrncpy(customExtBuffer, config.extensionFilter.c_str(), sizeof(customExtBuffer));
    fileBrowserData.customExtension = customExtBuffer;

    polystrncpy(packageMarkerBuffer, config.packageMarkerFile.c_str(), sizeof(packageMarkerBuffer));
    fileBrowserData.packageMarkerFile = packageMarkerBuffer;

    char** secPtrs = reinterpret_cast<char**>(scratchBase + kNamesBufferBytes + kBrowserListBytes +
                                              kSelectionListBytes);
    int secCount = std::min<int>(static_cast<int>(config.secondaryListItems.size()),
                                 kMaxSecondaryListItems);
    for (int i = 0; i < secCount; ++i)
    {
        secPtrs[i] = const_cast<char*>(config.secondaryListItems[i].c_str());
    }
    secondaryList.data = secPtrs;
    secondaryList.length = secCount;
    secondaryListPos = (secCount > 0) ? clamp(config.initialSecondaryIndex, 0, secCount - 1) : 0;
    secondaryList.start = secondaryListPos;

    // Normalize initialDirectory (strip leading and trailing slashes for FileBrowser)
    std::string initDir = config.initialDirectory;
    while (!initDir.empty() && initDir.front() == '/')
    {
        initDir.erase(initDir.begin());
    }
    while (!initDir.empty() && initDir.back() == '/')
    {
        initDir.pop_back();
    }

    FileSystem* fs = newFileManager.getFileSystem();
    if (fs != nullptr && isSaveMode() && !initDir.empty())
    {
        ensureDirRecursive(fs, initDir);
    }

    int restoredPos = 0;
    bool restoredFromContext = false;
    if (config.rememberLastPath && fs != nullptr)
    {
        std::string effKey = buildEffectiveContextKey();
        for (const auto& entry : s_savedContexts)
        {
            if (entry.valid && strcmp(entry.key, effKey.c_str()) == 0)
            {
                if (entry.path[0] == '\0' || fs->exists(entry.path))
                {
                    initDir = entry.path;
                    restoredPos = entry.position;
                    restoredFromContext = true;
                }
                break;
            }
        }
    }

    if (!restoredFromContext && fs != nullptr)
    {
        while (!initDir.empty() && !fs->exists(initDir.c_str()))
        {
            size_t slash = initDir.find_last_of('/');
            if (slash == std::string::npos)
            {
                initDir.clear();
            }
            else
            {
                initDir.resize(slash);
            }
        }
    }

    polystrncpy(fileBrowserData.currentPath, initDir.c_str(), sizeof(fileBrowserData.currentPath));
    fileBrowserData.defaultPath = fileBrowserData.currentPath;
    fileBrowserData.browserListPosition = restoredPos;

    if (fs != nullptr)
    {
        fileBrowser.start(fs, &fileBrowserData);
        rebuildExplorerDisplayLabels();
        if (restoredPos >= 0 && restoredPos < fileBrowserData.browserListLength)
        {
            fileBrowserData.browserListPosition = restoredPos;
        }
        else
        {
            fileBrowserData.browserListPosition = 0;
        }

        if (!config.initialSelectedName.empty())
        {
            std::string primaryExt = getPrimaryExtension();
            std::string withExt = config.initialSelectedName + primaryExt;
            for (int i = 0; i < fileBrowserData.browserListLength; ++i)
            {
                const char* item = fileBrowserData.browserList[i];
                const char* dispItem = config.onFormatItemLabel ? s_displayListPtrs[i] : nullptr;
                if (item == nullptr || item[0] == '/')
                {
                    continue;
                }
                if (strcasecmp(item, config.initialSelectedName.c_str()) == 0 ||
                    (!primaryExt.empty() && strcasecmp(item, withExt.c_str()) == 0) ||
                    (dispItem != nullptr &&
                     strcasecmp(dispItem, config.initialSelectedName.c_str()) == 0))
                {
                    fileBrowserData.browserListPosition = i;
                    break;
                }
            }
        }
    }

    rebuildFocusLayout();
    setMainFunctions();
    showBrowserScreen();
    if (config.notifyHighlightOnOpen)
    {
        notifyHighlightChanged();
    }
}

void cGlobalFileBrowser::stop()
{
    moduleRefresh = 0;
    mtPopups.hideInfoPopup();
    keyboardManager.deactivateKeyboard();
    stopFilePreview();
}

void cGlobalFileBrowser::update()
{
}

bool cGlobalFileBrowser::isSelectableItemSelected() const
{
    if (fileBrowserData.browserListLength <= 0)
    {
        return false;
    }
    auto& fb = const_cast<FileBrowser&>(fileBrowser);
    if (!fb.isRoot() && fileBrowserData.browserListPosition == 0)
    {
        return false; // "/.." parent entry
    }
    if (isPackageMode())
    {
        return fb.isFolderSelected() && fb.isPositionOnSelectionList();
    }
    return !fb.isFolderSelected();
}

bool cGlobalFileBrowser::canEnterSelectedFolder() const
{
    if (fileBrowserData.browserListLength <= 0)
    {
        return false;
    }
    auto& fb = const_cast<FileBrowser&>(fileBrowser);
    if (!fb.isRoot() && fileBrowserData.browserListPosition == 0)
    {
        return true; // "/.."
    }
    if (!fb.isFolderSelected())
    {
        return false;
    }
    // Plain directory (or directory that is not a leaf package) can be browsed into
    return !isPackageMode() || !fb.isPositionOnSelectionList();
}

bool cGlobalFileBrowser::canShowDeleteForCurrentItem() const
{
    if (!config.allowDelete || focusedPane != FocusPane::kExplorer ||
        fileBrowserData.browserListLength <= 0)
    {
        return false;
    }
    auto& fb = const_cast<FileBrowser&>(fileBrowser);
    if (!fb.isRoot() && fileBrowserData.browserListPosition == 0)
    {
        return false;
    }
    if (fb.isRoot() && fb.isFolderSelected())
    {
        if (isProtectedRootFolder(fb.getSelectedFolderName()))
        {
            return false;
        }
    }
    return true;
}

bool cGlobalFileBrowser::isFileSelected() const
{
    return isSelectableItemSelected();
}

std::string cGlobalFileBrowser::getSelectedPathIfSelectable() const
{
    return isSelectableItemSelected() ? getSelectedFullPath() : std::string{};
}

std::string cGlobalFileBrowser::getSelectedFullPath() const
{
    if (fileBrowserData.browserListLength <= 0)
    {
        return {};
    }
    auto& fb = const_cast<FileBrowser&>(fileBrowser);
    if (!fb.isRoot() && fileBrowserData.browserListPosition == 0)
    {
        return {};
    }
    char relPath[kMaxFilePathLen + 1]{};
    fb.getSelectedItemWithPath(relPath, sizeof(relPath));
    return std::string("/") + relPath;
}

std::string cGlobalFileBrowser::getPrimaryExtension() const
{
    const char* p = config.extensionFilter.c_str();
    while (*p == ';' || *p == ',' || *p == '|' || *p == ' ' || *p == '\t')
    {
        ++p;
    }
    std::string ext;
    while (*p != '\0' && *p != ';' && *p != ',' && *p != '|')
    {
        if (*p != ' ' && *p != '\t')
        {
            ext.push_back(*p);
        }
        ++p;
    }
    if (ext == "*" || ext == ".*")
    {
        return {};
    }
    if (ext.empty() && config.mode == GlobalFileBrowserConfig::Mode::kSaveWav)
    {
        return ".wav";
    }
    if (!ext.empty() && ext.front() != '.')
    {
        ext.insert(ext.begin(), '.');
    }
    return ext;
}

void cGlobalFileBrowser::notifyHighlightChanged()
{
    if (config.onHighlightChange && focusedPane == FocusPane::kExplorer)
    {
        config.onHighlightChange(getSelectedFullPath(), isSelectableItemSelected());
    }
}

void cGlobalFileBrowser::rebuildExplorerDisplayLabels()
{
    if (!config.onFormatItemLabel)
    {
        return;
    }

    int count = std::min<int>(fileBrowserData.browserListLength, kMaxFilesListLength);
    int firstFileIdx = count;

    for (int i = 0; i < count; ++i)
    {
        if (!fileBrowser.isRoot() && i == 0)
        {
            s_displayListPtrs[i] = fileBrowserData.browserList[i];
            continue;
        }

        bool folder = fileBrowser.isFolder(i);
        if (!folder && firstFileIdx == count)
        {
            firstFileIdx = i;
        }
        bool pkg = isPackageMode() && folder &&
                   (fileBrowserData.selectionList != nullptr &&
                    fileBrowserData.selectionList[i] != 0);

        const char* rawItem = fileBrowserData.browserList[i];
        const char* cleanName =
          (rawItem != nullptr && rawItem[0] == '/') ? (rawItem + 1) : (rawItem ? rawItem : "");

        std::string itemFullPath =
          fileBrowser.isRoot()
            ? (std::string("/") + cleanName)
            : (std::string("/") + fileBrowser.getCurrentPath() + "/" + cleanName);

        std::string customLabel =
          config.onFormatItemLabel(itemFullPath, cleanName, folder, pkg);
        if (!customLabel.empty())
        {
            polystrncpy(
              s_displayLabelsBuffer[i], customLabel.c_str(), sizeof(s_displayLabelsBuffer[i]));
            s_displayListPtrs[i] = s_displayLabelsBuffer[i];
        }
        else
        {
            s_displayListPtrs[i] = fileBrowserData.browserList[i];
        }
    }

    // Sort file entries alphabetically by their formatted display label, keeping browserList in sync
    for (int i = firstFileIdx; i + 1 < count; ++i)
    {
        for (int j = i + 1; j < count; ++j)
        {
            const char* labelA = s_displayListPtrs[i] ? s_displayListPtrs[i] : "";
            const char* labelB = s_displayListPtrs[j] ? s_displayListPtrs[j] : "";
            if (strcasecmp(labelA, labelB) > 0)
            {
                std::swap(fileBrowserData.browserList[i], fileBrowserData.browserList[j]);
                std::swap(s_displayListPtrs[i], s_displayListPtrs[j]);
                if (fileBrowserData.selectionList != nullptr)
                {
                    std::swap(fileBrowserData.selectionList[i], fileBrowserData.selectionList[j]);
                }
            }
        }
    }
}

void cGlobalFileBrowser::updateInspectorLabels()
{
    for (int i = 0; i < 4; ++i)
    {
        inspectorText[i][0] = '\0';
    }

    if (fileBrowserData.browserListLength <= 0)
    {
        polystrncpy(inspectorText[0], "Empty folder", sizeof(inspectorText[0]));
        return;
    }

    if (!fileBrowser.isRoot() && fileBrowserData.browserListPosition == 0)
    {
        polystrncpy(inspectorText[0], "Parent Folder", sizeof(inspectorText[0]));
        polystrncpy(inspectorText[1], "Press Enter to go up", sizeof(inspectorText[1]));
        return;
    }

    bool folder = fileBrowser.isFolderSelected();
    bool package = isPackageMode() && folder && fileBrowser.isPositionOnSelectionList();
    std::string fullPath = getSelectedFullPath();

    const char* rawName =
      folder ? fileBrowser.getSelectedFolderName() : fileBrowser.getSelectedItemName();
    const char* titleName = rawName;
    if (!folder && config.onFormatItemLabel && fileBrowserData.browserListPosition >= 0 &&
        fileBrowserData.browserListPosition < fileBrowserData.browserListLength &&
        s_displayListPtrs[fileBrowserData.browserListPosition] != nullptr)
    {
        titleName = s_displayListPtrs[fileBrowserData.browserListPosition];
    }
    polystrncpy(inspectorText[0], titleName ? titleName : "", sizeof(inspectorText[0]));

    if (config.onDescribeItem && !fullPath.empty())
    {
        std::string customDesc = config.onDescribeItem(fullPath, folder, package);
        if (!customDesc.empty())
        {
            int line = 1;
            size_t pos = 0;
            while (line < 4 && pos < customDesc.size())
            {
                size_t nl = customDesc.find('\n', pos);
                std::string chunk = (nl == std::string::npos)
                                      ? customDesc.substr(pos)
                                      : customDesc.substr(pos, nl - pos);
                polystrncpy(inspectorText[line], chunk.c_str(), sizeof(inspectorText[line]));
                ++line;
                if (nl == std::string::npos)
                {
                    break;
                }
                pos = nl + 1;
            }
            return;
        }
    }

    if (package)
    {
        if (config.packageMarkerFile == "project.mt")
        {
            polystrncpy(inspectorText[1], "Tracker Project", sizeof(inspectorText[1]));
        }
        else
        {
            polystrncpy(inspectorText[1], "Folder Package", sizeof(inspectorText[1]));
        }
        polystrncpy(inspectorText[2], "Package folder", sizeof(inspectorText[2]));
        return;
    }

    if (folder)
    {
        polystrncpy(inspectorText[1], "Folder", sizeof(inspectorText[1]));
        polystrncpy(inspectorText[2], "Press Enter to open", sizeof(inspectorText[2]));
        return;
    }

    FileSystem* fs = newFileManager.getFileSystem();
    char sizeBuf[32]{};
    if (fs != nullptr && !fullPath.empty())
    {
        formatFileSize(fs->getFileSize(fullPath.c_str()), sizeBuf, sizeof(sizeBuf));
    }

    if (rawName != nullptr && FileBrowser::checkFilenameExtension(rawName, ".wav"))
    {
        uint32_t sampleRate = 0;
        uint16_t channels = 0;
        uint16_t bits = 0;
        uint32_t dataBytes = 0;
        if (readWavMetadata(fs, fullPath.c_str(), sampleRate, channels, bits, dataBytes))
        {
            snprintf(inspectorText[1],
                     sizeof(inspectorText[1]),
                     "%lu Hz  %u-bit  %s",
                     static_cast<unsigned long>(sampleRate),
                     static_cast<unsigned>(bits),
                     channels == 1 ? "Mono" : "Stereo");
            uint32_t byteRate = sampleRate * channels * (bits / 8u);
            if (byteRate > 0)
            {
                uint32_t centisec = static_cast<uint32_t>(
                  (static_cast<uint64_t>(dataBytes) * 100ull) / byteRate);
                snprintf(inspectorText[2],
                         sizeof(inspectorText[2]),
                         "Length: %lu.%02lus  (%s)",
                         static_cast<unsigned long>(centisec / 100u),
                         static_cast<unsigned long>(centisec % 100u),
                         sizeBuf);
            }
            else
            {
                polystrncpy(inspectorText[2], sizeBuf, sizeof(inspectorText[2]));
            }
            return;
        }
    }

    const char* dot = (rawName != nullptr) ? strrchr(rawName, '.') : nullptr;
    if (dot != nullptr && dot[1] != '\0')
    {
        char extUpper[16]{};
        size_t idx = 0;
        for (const char* c = dot + 1; *c != '\0' && idx + 1 < sizeof(extUpper); ++c)
        {
            extUpper[idx++] = static_cast<char>(toupper(static_cast<unsigned char>(*c)));
        }
        snprintf(inspectorText[1], sizeof(inspectorText[1]), "%s File", extUpper);
    }
    else
    {
        polystrncpy(inspectorText[1], "File", sizeof(inspectorText[1]));
    }
    polystrncpy(inspectorText[2], sizeBuf, sizeof(inspectorText[2]));
}

void cGlobalFileBrowser::showBrowserScreen()
{
    titleBar->setTitle(config.title.c_str());
    titleBar->show();

    display.setControlShow(explorerLabelControl);
    display.setControlShow(explorerListControl);
    display.setControlShow(middleHeaderControl);
    display.setControlShow(rightCoverControl);
    display.setControlShow(frameControl);
    display.setControlShow(bgLabel);

    refreshBrowserScreen();
}

void cGlobalFileBrowser::refreshBrowserScreen()
{
    rebuildFocusLayout();

    if (fileBrowser.isRoot())
    {
        polystrncpy(explorerPathText, "/", sizeof(explorerPathText));
    }
    else
    {
        snprintf(explorerPathText, sizeof(explorerPathText), "/%s", fileBrowser.getCurrentPath());
    }

    const uint32_t* activeListTheme =
      isPackageMode() ? display.getTheme().projectActiveListColors
                      : display.getTheme().sampleLoaderActiveListColors;
    const uint32_t* inactiveListTheme =
      isPackageMode() ? display.getTheme().projectInactiveListColors
                      : display.getTheme().sampleLoaderInactiveListColors;

    const uint32_t* explorerListColors =
      (focusedPane == FocusPane::kExplorer) ? activeListTheme : inactiveListTheme;
    display.setControlColors(explorerListControl, explorerListColors);

    explorerHeaderColors[0] = explorerListColors[4];
    explorerHeaderColors[1] = explorerListColors[3];
    explorerHeaderColors[2] = explorerListColors[0];
    explorerHeaderColors[3] = explorerListColors[5];
    display.setControlColors(explorerLabelControl, explorerHeaderColors);
    display.setControlText(explorerLabelControl, explorerPathText);

    explorerList.data =
      config.onFormatItemLabel ? s_displayListPtrs : fileBrowserData.browserList;
    explorerList.length = fileBrowserData.browserListLength;
    explorerList.start = fileBrowserData.browserListPosition;
    explorerList.selectionActive = isPackageMode() ? 1 : 0;
    explorerList.selectTab =
      isPackageMode() ? reinterpret_cast<uint8_t*>(fileBrowserData.selectionList) : nullptr;
    display.setControlData(explorerListControl, &explorerList);
    display.setControlValue(explorerListControl, fileBrowserData.browserListPosition);

    bool secPane = hasSecondaryListPane();
    bool volPane = hasPreviewVolumePane();

    if (secPane)
    {
        display.setControlHide(middleCoverControl);
        for (int i = 0; i < 4; ++i)
        {
            display.setControlHide(inspectorLabel[i]);
        }

        const uint32_t* secColors =
          (focusedPane == FocusPane::kSecondaryList) ? activeListTheme : inactiveListTheme;
        middleHeaderColors[0] = secColors[4];
        middleHeaderColors[1] = secColors[3];
        middleHeaderColors[2] = secColors[0];
        middleHeaderColors[3] = secColors[5];
        polystrncpy(middleHeaderText,
                    config.secondaryListTitle.empty() ? "Recent" : config.secondaryListTitle.c_str(),
                    sizeof(middleHeaderText));
        display.setControlColors(middleHeaderControl, middleHeaderColors);
        display.setControlText(middleHeaderControl, middleHeaderText);
        display.setControlShow(middleHeaderControl);

        display.setControlColors(secondaryListControl, secColors);
        secondaryList.start = secondaryListPos;
        display.setControlData(secondaryListControl, &secondaryList);
        display.setControlValue(secondaryListControl, secondaryListPos);
        display.setControlShow(secondaryListControl);
    }
    else
    {
        display.setControlHide(secondaryListControl);
        display.setControlShow(middleCoverControl);

        middleHeaderColors[0] = inactiveListTheme[4];
        middleHeaderColors[1] = inactiveListTheme[3];
        middleHeaderColors[2] = inactiveListTheme[0];
        middleHeaderColors[3] = inactiveListTheme[5];
        polystrncpy(middleHeaderText, "Details", sizeof(middleHeaderText));
        display.setControlColors(middleHeaderControl, middleHeaderColors);
        display.setControlText(middleHeaderControl, middleHeaderText);
        display.setControlShow(middleHeaderControl);

        updateInspectorLabels();
        for (int i = 0; i < 4; ++i)
        {
            display.setControlText(inspectorLabel[i], inspectorText[i]);
            display.setControlShow(inspectorLabel[i]);
            display.refreshControl(inspectorLabel[i]);
        }
    }

    if (volPane)
    {
        display.setControlValue(previewVolumeBarControl, mtConfig.general.previewVolume);
        display.setControlShow(previewVolumeBarControl);
        display.setControlPosition(rightCoverControl, (800 / 8) * 7 + 1, 29);
        display.setControlSize(rightCoverControl, (800 / 8) * 1 - 2, 394);
    }
    else
    {
        display.setControlHide(previewVolumeBarControl);
        display.setControlPosition(rightCoverControl, (800 / 8) * 6 + 1, 29);
        display.setControlSize(rightCoverControl, (800 / 8) * 2 - 2, 394);
    }

    display.setControlValue(frameControl, selectedPlace);
    display.setControlShow(frameControl);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 0);
        display.setControlStyle(label[i],
                                controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
        display.setControlShow(label[i]);
    }

    bool selectable = (focusedPane == FocusPane::kExplorer)
                        ? isSelectableItemSelected()
                        : (focusedPane == FocusPane::kSecondaryList && secondaryList.length > 0);
    bool saveMode = isSaveMode();

    const char* defaultConfirmText =
      saveMode ? "Save Here" : (isPackageMode() ? "Open" : "Load");
    const char* confirmText =
      !config.confirmButtonLabel.empty() ? config.confirmButtonLabel.c_str() : defaultConfirmText;

    if (focusedPane == FocusPane::kExplorer && fileBrowserData.browserListLength > 0)
    {
        if (canEnterSelectedFolder())
        {
            display.setControlText(label[0], "Enter");
        }
        else if (!saveMode && selectable)
        {
            display.setControlText(label[0], confirmText);
        }
        else
        {
            display.setControlColors(label[0], display.getTheme().inactiveLabelsColors);
        }
    }
    else if (focusedPane == FocusPane::kSecondaryList && secondaryList.length > 0)
    {
        display.setControlText(label[0], confirmText);
    }
    else
    {
        display.setControlColors(label[0], display.getTheme().inactiveLabelsColors);
    }

    display.setControlText(label[1], "Delete");
    if (!canShowDeleteForCurrentItem())
    {
        display.setControlColors(label[1], display.getTheme().inactiveLabelsColors);
    }

    bool previewAvailable = (focusedPane == FocusPane::kExplorer) && isSelectableItemSelected() &&
                            (volPane || static_cast<bool>(config.onPreviewStart));
    display.setControlText(label[2], "Preview");
    if (!previewAvailable)
    {
        display.setControlColors(label[2], display.getTheme().inactiveLabelsColors);
    }

    if (focusedPane == FocusPane::kExplorer)
    {
        display.setControlText(label[3], "New Folder");
    }
    else
    {
        display.setControlColors(label[3], display.getTheme().inactiveLabelsColors);
    }

    display.setControlColors(label[4], display.getTheme().inactiveLabelsColors);
    display.setControlText(label[5], "Cancel");

    if (volPane)
    {
        display.setControlColors(label[6], display.getTheme().activeLabelsColors);
        display.setControlText(label[6], "Preview Vol");
        display.setControlValue(label[6], 1);
        updatePreviewVolumeText(focusedPane == FocusPane::kPreviewVolume);
    }
    else
    {
        display.setControlColors(label[6], display.getTheme().inactiveLabelsColors);
    }

    display.setControlText(label[7], confirmText);
    if (!saveMode && !selectable)
    {
        display.setControlColors(label[7], display.getTheme().inactiveLabelsColors);
    }

    display.refreshControl(explorerLabelControl);
    display.refreshControl(explorerListControl);
    display.refreshControl(middleHeaderControl);
    display.refreshControl(secondaryListControl);
    display.refreshControl(middleCoverControl);
    display.refreshControl(previewVolumeBarControl);
    display.refreshControl(rightCoverControl);
    display.refreshControl(frameControl);
    display.refreshControl(bgLabel);
    for (uint8_t i = 0; i < 8; i++)
    {
        display.refreshControl(label[i]);
    }
    display.synchronizeRefresh();
}

void cGlobalFileBrowser::updatePreviewVolumeText([[maybe_unused]] bool showDb)
{
    float db = 20.0f * log10f((float)mixAmpLogValues[mtConfig.general.previewVolume]);
    snprintf(previewVolumeText, sizeof(previewVolumeText), "%0.2f dB", (double)db);
    display.setControlText2(label[6], previewVolumeText);
    display.refreshControl(label[6]);
}

void cGlobalFileBrowser::setMainFunctions()
{
    ip->clearAll();

    ip->setPotObj(interfacePot0, gfbEncoderTurn);

    ip->setButtonObj(interfaceButtonUp, buttonPress, gfbArrowUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, gfbArrowDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, gfbArrowLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, gfbArrowRight);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, gfbEnterButton);
    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, gfbEnterButton);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, gfbDeleteButton);

    ip->setButtonObj(interfaceButton0, gfbBtn0);
    ip->setButtonObj(interfaceButton1, gfbBtn1);
    ip->setButtonObj(interfaceButton2, gfbBtn2);
    ip->setButtonObj(interfaceButton3, gfbBtn3);
    ip->setButtonObj(interfaceButton4, gfbBtn4);
    ip->setButtonObj(interfaceButton5, gfbBtn5);
    ip->setButtonObj(interfaceButton6, gfbBtn6);
    ip->setButtonObj(interfaceButton7, gfbBtn7);

    ip->setButtonObj(interfaceButtonParams, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonPerformance, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonGame, buttonPress, gfbSwitchModule);
    ip->setButtonObj(interfaceButtonMiniMenu, gfbMenu);

    ip->setPadsGlobal(gfbPadChange);
}

void cGlobalFileBrowser::handleLeftRight(int dir)
{
    bool secPane = hasSecondaryListPane();
    bool volPane = hasPreviewVolumePane();

    if (dir < 0)
    {
        if (focusedPane == FocusPane::kPreviewVolume)
        {
            focusedPane = secPane ? FocusPane::kSecondaryList : FocusPane::kExplorer;
            refreshBrowserScreen();
        }
        else if (focusedPane == FocusPane::kSecondaryList)
        {
            focusedPane = FocusPane::kExplorer;
            refreshBrowserScreen();
        }
        else if (focusedPane == FocusPane::kExplorer)
        {
            if (fileBrowserData.browserListLength > 0 && fileBrowserData.browserListPosition > 0)
            {
                stopFilePreview();
                fileBrowserData.browserListPosition = 0;
                saveCurrentContext();
                notifyHighlightChanged();
                refreshBrowserScreen();
                if (buttons->isButtonPressed(interfaceButton2))
                {
                    startFilePreview();
                }
            }
        }
    }
    else if (dir > 0)
    {
        if (focusedPane == FocusPane::kExplorer)
        {
            if (secPane)
            {
                stopFilePreview();
                focusedPane = FocusPane::kSecondaryList;
                refreshBrowserScreen();
            }
            else if (volPane)
            {
                focusedPane = FocusPane::kPreviewVolume;
                refreshBrowserScreen();
            }
        }
        else if (focusedPane == FocusPane::kSecondaryList && volPane)
        {
            focusedPane = FocusPane::kPreviewVolume;
            refreshBrowserScreen();
        }
    }
}

void cGlobalFileBrowser::handleEncoderOrArrows(int16_t delta, bool isEncoder)
{
    if (focusedPane == FocusPane::kPreviewVolume)
    {
        mtConfig.general.previewVolume = clamp((int)mtConfig.general.previewVolume + delta,
                                               (int)PREVIEW_VOLUME_MIN,
                                               (int)PREVIEW_VOLUME_MAX);
        engine.setPreviewVolume(mtConfig.general.previewVolume);
        saveConfig();
        display.setControlValue(previewVolumeBarControl, mtConfig.general.previewVolume);
        display.refreshControl(previewVolumeBarControl);
        updatePreviewVolumeText(true);
        display.synchronizeRefresh();
        return;
    }

    if (focusedPane == FocusPane::kSecondaryList)
    {
        if (secondaryList.length <= 0)
        {
            return;
        }
        bool invert = !isEncoder || interfaceGlobals.invertValForList;
        int step = invert ? -static_cast<int>(delta) : static_cast<int>(delta);
        int newPos = clamp(secondaryListPos + step, 0, secondaryList.length - 1);
        if (newPos != secondaryListPos)
        {
            secondaryListPos = newPos;
            refreshBrowserScreen();
        }
        return;
    }

    if (fileBrowserData.browserListLength <= 0)
    {
        return;
    }

    bool invert = !isEncoder || interfaceGlobals.invertValForList;
    int step = invert ? -static_cast<int>(delta) : static_cast<int>(delta);
    int prevPos = fileBrowserData.browserListPosition;
    int newPos = clamp(prevPos + step, 0, fileBrowserData.browserListLength - 1);

    if (newPos != prevPos)
    {
        stopFilePreview();
        fileBrowserData.browserListPosition = newPos;
        saveCurrentContext();
        notifyHighlightChanged();
        refreshBrowserScreen();
        if (buttons->isButtonPressed(interfaceButton2))
        {
            startFilePreview();
        }
    }
}

void cGlobalFileBrowser::handleEnterOrInsert()
{
    if (focusedPane == FocusPane::kSecondaryList)
    {
        confirmLoadSelectedItem();
        return;
    }

    if (focusedPane != FocusPane::kExplorer || fileBrowserData.browserListLength <= 0)
    {
        return;
    }

    if (canEnterSelectedFolder())
    {
        stopFilePreview();
        fileBrowser.browse();
        rebuildExplorerDisplayLabels();
        saveCurrentContext();
        notifyHighlightChanged();
        refreshBrowserScreen();
    }
    else if (!isSaveMode() && isSelectableItemSelected())
    {
        confirmLoadSelectedItem();
    }
}

void cGlobalFileBrowser::handleActionButtonPress(uint8_t button)
{
    switch (button)
    {
        case interfaceButton0:
            handleEnterOrInsert();
            break;
        case interfaceButton1:
            handleDeleteRequest();
            break;
        case interfaceButton2:
            startFilePreview();
            break;
        case interfaceButton3:
            if (focusedPane == FocusPane::kExplorer)
            {
                openNewFolderKeyboard();
            }
            break;
        case interfaceButton5:
            cancelBrowser();
            break;
        case interfaceButton6:
            if (hasPreviewVolumePane())
            {
                focusedPane = FocusPane::kPreviewVolume;
                refreshBrowserScreen();
            }
            break;
        case interfaceButton7:
            if (isSaveMode())
            {
                openSaveFileKeyboard();
            }
            else
            {
                confirmLoadSelectedItem();
            }
            break;
        default:
            break;
    }
}

void cGlobalFileBrowser::handleActionButtonRelease(uint8_t button)
{
    switch (button)
    {
        case interfaceButton2:
            stopFilePreview();
            break;
        case interfaceButton6:
            if (focusedPane == FocusPane::kPreviewVolume)
            {
                focusedPane = FocusPane::kExplorer;
                refreshBrowserScreen();
            }
            break;
        default:
            break;
    }
}

void cGlobalFileBrowser::handlePadInput(uint8_t pad, uint8_t state, int16_t velo)
{
    if (dialogState == DialogState::kBrowse && config.onPadEvent)
    {
        config.onPadEvent(pad, state, velo);
    }
}

void cGlobalFileBrowser::startFilePreview()
{
    if (focusedPane != FocusPane::kExplorer || !isSelectableItemSelected())
    {
        return;
    }

    std::string fullPath = getSelectedFullPath();
    if (fullPath.empty())
    {
        return;
    }

    if (config.onPreviewStart)
    {
        config.onPreviewStart(fullPath);
        return;
    }

    if (hasPreviewVolumePane())
    {
        auto sdPlayer = engine.getSdPlayer();
        if (sdPlayer != nullptr)
        {
            sdPlayer->stop();
            sdPlayer->play(fullPath.c_str(), 0, MAX_16BIT);
        }
    }
}

void cGlobalFileBrowser::stopFilePreview()
{
    if (config.onPreviewStop)
    {
        config.onPreviewStop();
    }
    if (hasPreviewVolumePane())
    {
        auto sdPlayer = engine.getSdPlayer();
        if (sdPlayer != nullptr)
        {
            sdPlayer->stop();
        }
    }
}

void cGlobalFileBrowser::openNewFolderKeyboard()
{
    stopFilePreview();
    keyboardPurpose = KeyboardPurpose::kNewFolder;
    keyboardManager.fillName("New Folder");
    keyboardManager.activateKeyboard(30);
    showKeyboardScreen("Create New Folder", "Create");
}

void cGlobalFileBrowser::openSaveFileKeyboard()
{
    stopFilePreview();
    keyboardPurpose = KeyboardPurpose::kSaveFile;

    char initialName[64]{};
    if (pendingEnteredName[0] != '\0')
    {
        polystrncpy(initialName, pendingEnteredName, sizeof(initialName));
        pendingEnteredName[0] = '\0';
    }
    else if (focusedPane == FocusPane::kExplorer && isSelectableItemSelected())
    {
        if (isPackageMode())
        {
            polystrncpy(initialName, fileBrowser.getSelectedFolderName(), sizeof(initialName));
        }
        else
        {
            polystrncpy(initialName, fileBrowser.getSelectedItemName(), sizeof(initialName));
            char* dot = strrchr(initialName, '.');
            if (dot != nullptr)
            {
                *dot = '\0';
            }
        }
    }
    else
    {
        polystrncpy(initialName, config.defaultFileName.c_str(), sizeof(initialName));
        if (!isPackageMode())
        {
            char* dot = strrchr(initialName, '.');
            if (dot != nullptr)
            {
                *dot = '\0';
            }
        }
    }

    if (initialName[0] == '\0')
    {
        polystrncpy(initialName, isPackageMode() ? "project" : "sample", sizeof(initialName));
    }

    uint8_t maxLen = clamp<uint8_t>(config.maxNameLength, 1, 60);
    keyboardManager.fillName(initialName);
    keyboardManager.activateKeyboard(maxLen);
    showKeyboardScreen(config.title.c_str(), "Save");
}

void cGlobalFileBrowser::showKeyboardScreen(const char* titleText, const char* confirmLabel)
{
    titleBar->setTitle(titleText);

    display.setControlHide(explorerLabelControl);
    display.setControlHide(explorerListControl);
    display.setControlHide(middleHeaderControl);
    display.setControlHide(secondaryListControl);
    display.setControlHide(middleCoverControl);
    display.setControlHide(previewVolumeBarControl);
    display.setControlHide(rightCoverControl);
    display.setControlHide(frameControl);
    for (int i = 0; i < 4; ++i)
    {
        display.setControlHide(inspectorLabel[i]);
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 0);
        display.setControlStyle(label[i],
                                controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
        display.setControlShow(label[i]);
    }

    display.setControlText(label[0], "Enter");
    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, gfbEnterButton);
    display.setControlText(label[1], "Auto Name");
    display.setControlColors(label[2], display.getTheme().inactiveLabelsColors);
    display.setControlColors(label[3], display.getTheme().inactiveLabelsColors);
    display.setControlColors(label[4], display.getTheme().inactiveLabelsColors);
    display.setControlColors(label[5], display.getTheme().inactiveLabelsColors);
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], confirmLabel);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.refreshControl(label[i]);
    }
    display.synchronizeRefresh();
}

void cGlobalFileBrowser::showConfirmPopup(const char* line1,
                                          const char* line2,
                                          const char* confirmLabel)
{
    display.setControlHide(frameControl);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 0);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }

    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], confirmLabel);
    display.setControlColors(label[6], display.getTheme().highlightedButtonLabelsColors);
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    display.setRemoveControlStyle(label[6], controlStyleShowBitmap);
    display.setAddControlStyle(label[6], controlStyleBackground);
    display.setAddControlStyle(label[7], controlStyleBackground);
    display.refreshControl(label[6]);
    display.refreshControl(label[7]);

    display.setControlValue(bgLabel, 255);
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);

    mtPopups.showInfoPopup(line1, line2);
    display.synchronizeRefresh();
}

void cGlobalFileBrowser::handleKeyboardButtonPress(uint8_t button)
{
    switch (button)
    {
        case interfaceButton0:
            keyboardManager.confirmKey();
            break;
        case interfaceButton1:
            keyboardManager.setRandomName();
            break;
        case interfaceButton6:
            cancelKeyboard();
            break;
        case interfaceButton7:
            confirmKeyboard();
            break;
        default:
            break;
    }
}

void cGlobalFileBrowser::cancelKeyboard()
{
    pendingEnteredName[0] = '\0';
    keyboardPurpose = KeyboardPurpose::kNone;
    keyboardManager.deactivateKeyboard();
    showBrowserScreen();
}

void cGlobalFileBrowser::executeCommitSave(const std::string& fullPath)
{
    saveCurrentContext();
    auto onConfirmCb = config.onConfirm;
    returnToCallerModule();
    if (onConfirmCb)
    {
        onConfirmCb(fullPath);
    }
}

void cGlobalFileBrowser::confirmKeyboard()
{
    char enteredName[64]{};
    polystrncpy(enteredName, keyboardManager.getName(), sizeof(enteredName));

    if (enteredName[0] == '\0')
    {
        return;
    }

    if (keyboardPurpose == KeyboardPurpose::kNewFolder)
    {
        keyboardPurpose = KeyboardPurpose::kNone;
        keyboardManager.deactivateKeyboard();

        if (!fileBrowser.createFolderInCurrentPath(enteredName))
        {
            mtPopups.show(1, "Folder already exists");
        }
        else
        {
            fileBrowser.refresh();
            rebuildExplorerDisplayLabels();
            fileBrowser.selectSpecificFolderName(enteredName);
            saveCurrentContext();
            notifyHighlightChanged();
        }
        showBrowserScreen();
        return;
    }

    if (keyboardPurpose == KeyboardPurpose::kSaveFile)
    {
        keyboardPurpose = KeyboardPurpose::kNone;
        keyboardManager.deactivateKeyboard();
        polystrncpy(pendingEnteredName, enteredName, sizeof(pendingEnteredName));

        FileSystem* fs = newFileManager.getFileSystem();
        std::string fullPath;
        bool alreadyExists = false;

        if (isPackageMode())
        {
            std::string nameStr(enteredName);
            if (fileBrowser.isRoot())
            {
                fullPath = "/" + nameStr;
            }
            else
            {
                fullPath = std::string("/") + fileBrowser.getCurrentPath() + "/" + nameStr;
            }

            if (config.confirmOverwrite && fs != nullptr)
            {
                const char* relCheck = fullPath.c_str() + 1;
                std::string markerCheck = std::string(relCheck) + "/" +
                                          (packageMarkerBuffer[0] ? packageMarkerBuffer
                                                                  : "project.mt");
                alreadyExists = fs->exists(markerCheck.c_str()) || fs->exists(relCheck);
            }
        }
        else
        {
            std::string fileNameStr(enteredName);
            std::string primaryExt = getPrimaryExtension();
            if (!primaryExt.empty() &&
                !FileBrowser::checkFilenameExtension(fileNameStr.c_str(),
                                                     config.extensionFilter.c_str()))
            {
                fileNameStr += primaryExt;
            }

            if (fileBrowser.isRoot())
            {
                fullPath = "/" + fileNameStr;
            }
            else
            {
                fullPath = std::string("/") + fileBrowser.getCurrentPath() + "/" + fileNameStr;
            }

            if (config.confirmOverwrite && fs != nullptr)
            {
                alreadyExists = fs->exists(fullPath.c_str());
            }
        }

        if (alreadyExists)
        {
            pendingSavePath = fullPath;
            dialogState = DialogState::kConfirmOverwrite;
            snprintf(confirmInfoText, sizeof(confirmInfoText), "\"%s\"?", enteredName);
            showConfirmPopup(
              isPackageMode() ? "Do you want overwrite project" : "Do you want overwrite file",
              confirmInfoText,
              "Overwrite");
            return;
        }

        pendingEnteredName[0] = '\0';
        executeCommitSave(fullPath);
    }
}

void cGlobalFileBrowser::handleDeleteRequest()
{
    if (!canShowDeleteForCurrentItem())
    {
        return;
    }

    stopFilePreview();

    std::string fullPath = getSelectedFullPath();
    if (fullPath.empty())
    {
        return;
    }

    bool isFolder = fileBrowser.isFolderSelected();
    bool isPkg = isPackageMode() && isFolder && fileBrowser.isPositionOnSelectionList();

    if (config.canDelete)
    {
        std::string denyReason;
        if (!config.canDelete(fullPath, isFolder, isPkg, denyReason))
        {
            if (!denyReason.empty())
            {
                mtPopups.show(1, denyReason.c_str());
            }
            return;
        }
    }

    const char* itemName =
      isFolder ? fileBrowser.getSelectedFolderName() : fileBrowser.getSelectedItemName();
    snprintf(confirmInfoText, sizeof(confirmInfoText), "\"%s\"?", itemName ? itemName : "");

    const char* promptLine =
      isPkg ? "Do you want to delete project:"
            : (isFolder ? "Do you want to delete folder:" : "Do you want to delete file:");

    dialogState = DialogState::kConfirmDelete;
    showConfirmPopup(promptLine, confirmInfoText, "Delete");
}

void cGlobalFileBrowser::confirmDialogAction()
{
    DialogState state = dialogState;
    dialogState = DialogState::kBrowse;
    mtPopups.hideInfoPopup();

    if (state == DialogState::kConfirmOverwrite)
    {
        std::string pathToSave = pendingSavePath;
        pendingSavePath.clear();
        pendingEnteredName[0] = '\0';
        executeCommitSave(pathToSave);
        return;
    }

    if (state == DialogState::kConfirmDelete)
    {
        std::string fullPath = getSelectedFullPath();
        bool isFolder = fileBrowser.isFolderSelected();
        bool isPkg = isPackageMode() && isFolder && fileBrowser.isPositionOnSelectionList();

        bool deleted = false;
        if (!fullPath.empty())
        {
            if (config.onDelete)
            {
                deleted = config.onDelete(fullPath, isFolder, isPkg);
            }
            else
            {
                FileSystem* fs = newFileManager.getFileSystem();
                if (fs != nullptr)
                {
                    const char* relPath =
                      (fullPath[0] == '/') ? (fullPath.c_str() + 1) : fullPath.c_str();
                    deleted = isFolder ? fs->removeDirWithFiles(relPath) : fs->remove(relPath);
                }
            }
        }

        if (deleted)
        {
            int prevPos = fileBrowserData.browserListPosition;
            fileBrowser.refresh();
            rebuildExplorerDisplayLabels();
            if (fileBrowserData.browserListLength > 0)
            {
                fileBrowserData.browserListPosition =
                  clamp(prevPos, 0, fileBrowserData.browserListLength - 1);
            }
            saveCurrentContext();
            notifyHighlightChanged();
        }
        else
        {
            mtPopups.show(1, "Error deleting item");
        }

        showBrowserScreen();
    }
}

void cGlobalFileBrowser::cancelDialogAction()
{
    DialogState state = dialogState;
    dialogState = DialogState::kBrowse;
    mtPopups.hideInfoPopup();

    if (state == DialogState::kConfirmOverwrite)
    {
        pendingSavePath.clear();
        openSaveFileKeyboard();
        return;
    }

    if (state == DialogState::kConfirmDelete)
    {
        showBrowserScreen();
    }
}

void cGlobalFileBrowser::confirmLoadSelectedItem()
{
    if (focusedPane == FocusPane::kSecondaryList)
    {
        if (secondaryList.length <= 0 || secondaryListPos < 0 ||
            secondaryListPos >= static_cast<int>(config.secondaryListItems.size()))
        {
            return;
        }
        stopFilePreview();
        saveCurrentContext();
        int idx = secondaryListPos;
        std::string item = config.secondaryListItems[idx];
        auto onSecConfirmCb = config.onSecondaryConfirm;
        returnToCallerModule();
        if (onSecConfirmCb)
        {
            onSecConfirmCb(idx, item);
        }
        return;
    }

    if (focusedPane != FocusPane::kExplorer || !isSelectableItemSelected())
    {
        return;
    }

    stopFilePreview();
    saveCurrentContext();

    std::string fullPath = getSelectedFullPath();
    auto onConfirmCb = config.onConfirm;
    returnToCallerModule();
    if (onConfirmCb)
    {
        onConfirmCb(fullPath);
    }
}

void cGlobalFileBrowser::cancelBrowser()
{
    stopFilePreview();
    saveCurrentContext();
    auto onCancelCb = config.onCancel;
    returnToCallerModule();
    if (onCancelCb)
    {
        onCancelCb();
    }
}

void cGlobalFileBrowser::returnToCallerModule()
{
    ModuleBase* retMod = config.returnModule;
    uint32_t retOpts = config.returnModuleOptions;
    ModuleBase* prevMod = savedPrevModule;
    uint32_t prevOpts = savedPrevModuleOptions;
    savedPrevModule = nullptr;
    savedPrevModuleOptions = 0;
    mtInterface.deactivateModule(this);
    if (retMod != nullptr)
    {
        mtInterface.activateModule(retMod, retOpts);
    }
    mtInterface.setPreviousModule(prevMod, prevOpts);
}
