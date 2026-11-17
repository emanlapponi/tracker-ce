#ifndef INTERFACE_FILEBROWSER_GLOBALFILEBROWSER_H_
#define INTERFACE_FILEBROWSER_GLOBALFILEBROWSER_H_

#include "FileBrowser.h"
#include "core/modulesBase.h"
#include "mtKeyboardManager.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct GlobalFileBrowserConfig
{
    enum class Mode : uint8_t
    {
        kLoadWav,
        kSaveWav,
        kLoadGenericFile,
        kSaveGenericFile,
        kLoadFolderPackage,
        kSaveFolderPackage
    };

    Mode mode = Mode::kLoadWav;
    std::string title = "Load File";
    std::string initialDirectory = "/Samples";
    std::string defaultFileName = "sample";
    std::string extensionFilter = ".wav";
    ModuleBase* returnModule = nullptr;
    uint32_t returnModuleOptions = 0;
    std::function<void(const std::string& fullPath)> onConfirm;
    std::function<void()> onCancel;

    std::string contextKey = "";
    bool rememberLastPath = true;
    bool confirmOverwrite = true;
    bool allowDelete = true;
    std::string packageMarkerFile = "project.mt";
    std::string confirmButtonLabel = "";
    uint8_t maxNameLength = 30;

    std::function<void(const std::string& fullPath)> onPreviewStart;
    std::function<void()> onPreviewStop;
    std::function<void(const std::string& fullPath, bool isSelectable)> onHighlightChange;
    std::function<void(uint8_t pad, uint8_t state, int16_t velo)> onPadEvent;
    std::function<std::string(const std::string& fullPath, bool isFolder, bool isPackage)>
      onDescribeItem;
    std::function<std::string(const std::string& fullPath,
                              const char* rawName,
                              bool isFolder,
                              bool isPackage)>
      onFormatItemLabel;
    std::function<bool(const std::string& fullPath,
                       bool isFolder,
                       bool isPackage,
                       std::string& outDenyReason)>
      canDelete;
    std::function<bool(const std::string& fullPath, bool isFolder, bool isPackage)> onDelete;

    std::string secondaryListTitle = "";
    std::vector<std::string> secondaryListItems;
    int initialSecondaryIndex = 0;
    std::function<void(int index, const std::string& item)> onSecondaryConfirm;

    std::string initialSelectedName = "";
    bool notifyHighlightOnOpen = true;
};

class cGlobalFileBrowser : public ModuleBase
{
  public:
    cGlobalFileBrowser() = default;
    virtual ~cGlobalFileBrowser() {}

    void initDisplayControls() override;
    void destroyDisplayControls() override {}
    void start(uint32_t options) override;
    void stop() override;
    void update() override;

    void configure(const GlobalFileBrowserConfig& cfg);

    void showBrowserScreen();
    void refreshBrowserScreen();
    void showKeyboardScreen(const char* titleText, const char* confirmLabel);

    void setMainFunctions();

    void handleEncoderOrArrows(int16_t delta, bool isEncoder);
    void handleLeftRight(int dir);
    void handleEnterOrInsert();
    void handleActionButtonPress(uint8_t button);
    void handleActionButtonRelease(uint8_t button);
    void handleKeyboardButtonPress(uint8_t button);
    void handleDeleteRequest();
    void handlePadInput(uint8_t pad, uint8_t state, int16_t velo);

    void startFilePreview();
    void stopFilePreview();
    void updatePreviewVolumeText(bool showDb);

    void openNewFolderKeyboard();
    void openSaveFileKeyboard();
    void confirmKeyboard();
    void cancelKeyboard();

    void confirmLoadSelectedItem();
    void cancelBrowser();

    void confirmDialogAction();
    void cancelDialogAction();

    bool isSaveMode() const;
    bool isPackageMode() const;
    bool hasPreviewVolumePane() const;
    bool hasSecondaryListPane() const;
    bool isSelectableItemSelected() const;
    bool canEnterSelectedFolder() const;
    bool canShowDeleteForCurrentItem() const;
    bool isFileSelected() const;
    std::string getSelectedPathIfSelectable() const;

    GlobalFileBrowserConfig config;
    FileBrowser fileBrowser;
    FileBrowserData fileBrowserData{};
    strList explorerList{};
    strList secondaryList{};
    strFrameData frameData{};

    enum class FocusPane : uint8_t
    {
        kExplorer = 0,
        kSecondaryList = 1,
        kPreviewVolume = 2
    };

    enum class DialogState : uint8_t
    {
        kBrowse = 0,
        kConfirmOverwrite,
        kConfirmDelete
    };

    FocusPane focusedPane = FocusPane::kExplorer;
    uint8_t selectedPlace = 0;
    DialogState dialogState = DialogState::kBrowse;

    enum class KeyboardPurpose : uint8_t
    {
        kNone = 0,
        kNewFolder,
        kSaveFile
    };
    KeyboardPurpose keyboardPurpose = KeyboardPurpose::kNone;
    ModuleBase* savedPrevModule = nullptr;
    uint32_t savedPrevModuleOptions = 0;

  private:
    void returnToCallerModule();
    void rebuildFocusLayout();
    void rebuildExplorerDisplayLabels();
    void updateInspectorLabels();
    void notifyHighlightChanged();
    void saveCurrentContext() const;
    std::string buildEffectiveContextKey() const;
    std::string getSelectedFullPath() const;
    std::string getPrimaryExtension() const;
    void showConfirmPopup(const char* line1, const char* line2, const char* confirmLabel);
    void executeCommitSave(const std::string& fullPath);

    hControl label[8]{};
    hControl bgLabel = -1;
    hControl frameControl = -1;
    hControl explorerLabelControl = -1;
    hControl explorerListControl = -1;
    hControl middleHeaderControl = -1;
    hControl secondaryListControl = -1;
    hControl inspectorLabel[4]{};
    hControl previewVolumeBarControl = -1;
    hControl middleCoverControl = -1;
    hControl rightCoverControl = -1;

    char explorerPathText[kMaxFilePathLen + 4]{};
    char middleHeaderText[32]{};
    char customExtBuffer[48]{};
    char packageMarkerBuffer[32]{};
    char previewVolumeText[24]{};
    char inspectorText[4][64]{};
    char confirmInfoText[128]{};
    char pendingEnteredName[64]{};
    std::string pendingSavePath;
    int secondaryListPos = 0;
    uint32_t explorerHeaderColors[4]{};
    uint32_t middleHeaderColors[4]{};
};

extern cGlobalFileBrowser globalFileBrowser;

void openGlobalFileBrowser(const GlobalFileBrowserConfig& config);

namespace GlobalFileBrowser
{
inline void open(const GlobalFileBrowserConfig& config)
{
    openGlobalFileBrowser(config);
}

inline void loadWav(std::string title,
                    std::string initialDirectory,
                    ModuleBase* returnModule,
                    std::function<void(const std::string& fullPath)> onConfirm,
                    std::string contextKey = "",
                    uint32_t returnModuleOptions = 0)
{
    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kLoadWav;
    cfg.title = std::move(title);
    cfg.initialDirectory = std::move(initialDirectory);
    cfg.extensionFilter = ".wav";
    cfg.returnModule = returnModule;
    cfg.returnModuleOptions = returnModuleOptions;
    cfg.onConfirm = std::move(onConfirm);
    cfg.contextKey = std::move(contextKey);
    openGlobalFileBrowser(cfg);
}

inline void saveWav(std::string title,
                    std::string initialDirectory,
                    std::string defaultFileName,
                    ModuleBase* returnModule,
                    std::function<void(const std::string& fullPath)> onConfirm,
                    std::string contextKey = "",
                    uint32_t returnModuleOptions = 0)
{
    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kSaveWav;
    cfg.title = std::move(title);
    cfg.initialDirectory = std::move(initialDirectory);
    cfg.defaultFileName = std::move(defaultFileName);
    cfg.extensionFilter = ".wav";
    cfg.returnModule = returnModule;
    cfg.returnModuleOptions = returnModuleOptions;
    cfg.onConfirm = std::move(onConfirm);
    cfg.contextKey = std::move(contextKey);
    openGlobalFileBrowser(cfg);
}

inline void loadFile(std::string title,
                     std::string initialDirectory,
                     std::string extensionFilter,
                     ModuleBase* returnModule,
                     std::function<void(const std::string& fullPath)> onConfirm,
                     std::string contextKey = "",
                     uint32_t returnModuleOptions = 0)
{
    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kLoadGenericFile;
    cfg.title = std::move(title);
    cfg.initialDirectory = std::move(initialDirectory);
    cfg.extensionFilter = std::move(extensionFilter);
    cfg.returnModule = returnModule;
    cfg.returnModuleOptions = returnModuleOptions;
    cfg.onConfirm = std::move(onConfirm);
    cfg.contextKey = std::move(contextKey);
    openGlobalFileBrowser(cfg);
}

inline void saveFile(std::string title,
                     std::string initialDirectory,
                     std::string defaultFileName,
                     std::string extensionFilter,
                     ModuleBase* returnModule,
                     std::function<void(const std::string& fullPath)> onConfirm,
                     std::string contextKey = "",
                     uint32_t returnModuleOptions = 0)
{
    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kSaveGenericFile;
    cfg.title = std::move(title);
    cfg.initialDirectory = std::move(initialDirectory);
    cfg.defaultFileName = std::move(defaultFileName);
    cfg.extensionFilter = std::move(extensionFilter);
    cfg.returnModule = returnModule;
    cfg.returnModuleOptions = returnModuleOptions;
    cfg.onConfirm = std::move(onConfirm);
    cfg.contextKey = std::move(contextKey);
    openGlobalFileBrowser(cfg);
}

inline void loadPackageFolder(std::string title,
                              std::string initialDirectory,
                              std::string markerFile,
                              ModuleBase* returnModule,
                              std::function<void(const std::string& fullPath)> onConfirm,
                              std::string contextKey = "",
                              uint32_t returnModuleOptions = 0)
{
    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kLoadFolderPackage;
    cfg.title = std::move(title);
    cfg.initialDirectory = std::move(initialDirectory);
    cfg.packageMarkerFile = std::move(markerFile);
    cfg.returnModule = returnModule;
    cfg.returnModuleOptions = returnModuleOptions;
    cfg.onConfirm = std::move(onConfirm);
    cfg.contextKey = std::move(contextKey);
    openGlobalFileBrowser(cfg);
}

inline void savePackageFolder(std::string title,
                              std::string initialDirectory,
                              std::string defaultName,
                              std::string markerFile,
                              ModuleBase* returnModule,
                              std::function<void(const std::string& fullPath)> onConfirm,
                              std::string contextKey = "",
                              uint32_t returnModuleOptions = 0)
{
    GlobalFileBrowserConfig cfg;
    cfg.mode = GlobalFileBrowserConfig::Mode::kSaveFolderPackage;
    cfg.title = std::move(title);
    cfg.initialDirectory = std::move(initialDirectory);
    cfg.defaultFileName = std::move(defaultName);
    cfg.packageMarkerFile = std::move(markerFile);
    cfg.returnModule = returnModule;
    cfg.returnModuleOptions = returnModuleOptions;
    cfg.onConfirm = std::move(onConfirm);
    cfg.contextKey = std::move(contextKey);
    openGlobalFileBrowser(cfg);
}
} // namespace GlobalFileBrowser

#endif /* INTERFACE_FILEBROWSER_GLOBALFILEBROWSER_H_ */
