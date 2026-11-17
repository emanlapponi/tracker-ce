#include "ptfx_manager.h"
#include "Platform.h"
#include "TrackerImpl.h"
#include "sampleEditor/sampleEditor.h"

#include <cr_section_macros.h>
#include <cstring>
#include <strings.h>

__NOINIT(EXTERNAL_RAM) PtfxEntry PtfxManager::s_activeEntry;
__NOINIT(EXTERNAL_RAM) PtfxHeader PtfxManager::s_activeHeader;
__NOINIT(EXTERNAL_RAM) char* PtfxManager::s_effectDisplayNames[kBuiltInEffectsCount + 2];

bool PtfxManager::s_initialized = false;
bool PtfxManager::s_hasLoadedPlugin = false;

static bool s_effectsDirChecked = false;

void PtfxManager::rebuildEffectDisplayNames()
{
    memset(s_effectDisplayNames, 0, sizeof(s_effectDisplayNames));

    for (uint16_t i = 0; i < kBuiltInEffectsCount; i++)
    {
        s_effectDisplayNames[i] = effectNamesLabels[i];
    }

    if (s_hasLoadedPlugin)
    {
        s_effectDisplayNames[kBuiltInEffectsCount] = s_activeEntry.displayName;
        s_effectDisplayNames[kBuiltInEffectsCount + 1] = const_cast<char*>("Load Effect...");
    }
    else
    {
        s_effectDisplayNames[kBuiltInEffectsCount] = const_cast<char*>("Load Effect...");
        s_effectDisplayNames[kBuiltInEffectsCount + 1] = nullptr;
    }
}

void PtfxManager::initEffectList()
{
    if (!s_initialized)
    {
        memset(&s_activeEntry, 0, sizeof(s_activeEntry));
        memset(&s_activeHeader, 0, sizeof(s_activeHeader));
        s_hasLoadedPlugin = false;
        s_initialized = true;
    }

    if (!s_effectsDirChecked)
    {
        TrackerImpl* tracker = TrackerImpl::getInstance();
        Platform* platform = tracker ? tracker->getPlatform() : nullptr;
        FileSystem* fs = platform ? platform->getFileSystem() : nullptr;
        if (fs && fs->isSdCardInserted())
        {
            if (!fs->exists("/Effects"))
            {
                fs->mkdir(0, "/Effects");
            }
            s_effectsDirChecked = true;
        }
    }

    rebuildEffectDisplayNames();
}

bool PtfxManager::hasLoadedPlugin()
{
    return s_hasLoadedPlugin;
}

const PtfxEntry* PtfxManager::getActivePlugin()
{
    return s_hasLoadedPlugin ? &s_activeEntry : nullptr;
}

const char* PtfxManager::getActivePluginDisplayName()
{
    return s_hasLoadedPlugin ? s_activeEntry.displayName : "";
}

const PtfxHeader& PtfxManager::getActiveHeader()
{
    return s_activeHeader;
}

bool PtfxManager::loadPluginFromPath(const char* filepath, mtEffectModular& modularEffect)
{
    if (!filepath || filepath[0] == '\0')
        return false;

    PtfxHeader           tempHeader{};
    PtfxInitFunc         initFunc         = nullptr;
    PtfxSetParamFunc     setParamFunc     = nullptr;
    PtfxProcessBlockFunc processBlockFunc = nullptr;

    bool loaded = PtfxLoader::loadPlugin(
      filepath, tempHeader, initFunc, setParamFunc, processBlockFunc);

    if (!loaded)
        return false;

    memcpy(&s_activeHeader, &tempHeader, sizeof(PtfxHeader));

    strncpy(s_activeEntry.filepath, filepath, sizeof(s_activeEntry.filepath) - 1);
    s_activeEntry.filepath[sizeof(s_activeEntry.filepath) - 1] = '\0';

    const char* slash = strrchr(filepath, '/');
    const char* baseName = (slash != nullptr) ? (slash + 1) : filepath;
    strncpy(s_activeEntry.filename, baseName, sizeof(s_activeEntry.filename) - 1);
    s_activeEntry.filename[sizeof(s_activeEntry.filename) - 1] = '\0';

    s_activeEntry.flags = s_activeHeader.flags;

    if (s_activeHeader.display_name[0] != '\0')
    {
        strncpy(s_activeEntry.displayName, s_activeHeader.display_name, sizeof(s_activeEntry.displayName) - 1);
    }
    else
    {
        strncpy(s_activeEntry.displayName, s_activeEntry.filename, sizeof(s_activeEntry.displayName) - 1);
        size_t nameLen = strlen(s_activeEntry.displayName);
        if (nameLen > 5 && strcasecmp(s_activeEntry.displayName + nameLen - 5, ".ptfx") == 0)
        {
            s_activeEntry.displayName[nameLen - 5] = '\0';
        }
    }
    s_activeEntry.displayName[sizeof(s_activeEntry.displayName) - 1] = '\0';

    s_hasLoadedPlugin = true;
    s_initialized = true;
    rebuildEffectDisplayNames();

    modularEffect.configurePlugin(s_activeHeader,
                                  PtfxLoader::getScratchBuffer(),
                                  PtfxLoader::getMaxScratchSize(),
                                  PtfxLoader::getHostApi(),
                                  initFunc,
                                  setParamFunc,
                                  processBlockFunc);

    return true;
}

bool PtfxManager::reloadActivePlugin(mtEffectModular& modularEffect)
{
    if (!s_hasLoadedPlugin || s_activeEntry.filepath[0] == '\0')
        return false;

    return loadPluginFromPath(s_activeEntry.filepath, modularEffect);
}

char** PtfxManager::getEffectListNames()
{
    return s_effectDisplayNames;
}

uint16_t PtfxManager::getTotalEffectListCount()
{
    return s_hasLoadedPlugin ? (kBuiltInEffectsCount + 2) : (kBuiltInEffectsCount + 1);
}

bool PtfxManager::isLoadedPluginListIndex(uint16_t listIndex)
{
    return s_hasLoadedPlugin && (listIndex == kBuiltInEffectsCount);
}

bool PtfxManager::isLoadEffectListIndex(uint16_t listIndex)
{
    return listIndex == getLoadEffectListIndex();
}

uint16_t PtfxManager::getLoadEffectListIndex()
{
    return s_hasLoadedPlugin ? (kBuiltInEffectsCount + 1) : kBuiltInEffectsCount;
}
