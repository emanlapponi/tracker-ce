#ifndef PTFX_MANAGER_H_
#define PTFX_MANAGER_H_

#include "mtEffectModular.h"
#include "ptfx_loader.h"
#include "tracker_plugin.h"
#include <stdint.h>

static constexpr uint16_t kBuiltInEffectsCount = 17;

struct PtfxEntry
{
    char     filepath[256];
    char     filename[42];
    char     displayName[32];
    uint16_t flags;
};

class PtfxManager
{
  public:
    static void initEffectList();
    static bool hasLoadedPlugin();
    static const PtfxEntry* getActivePlugin();
    static const char* getActivePluginDisplayName();

    // Loads a .ptfx plugin from an SD path into executable RAM and configures mtEffectModular
    static bool loadPluginFromPath(const char* filepath, mtEffectModular& modularEffect);

    // Reloads the currently active plugin from its stored filepath
    static bool reloadActivePlugin(mtEffectModular& modularEffect);

    // Active loaded plugin header (single in-RAM instance)
    static const PtfxHeader& getActiveHeader();

    // Effect list helpers for cSampleEditor::effectListData
    static char** getEffectListNames();
    static uint16_t getTotalEffectListCount();
    static bool isLoadedPluginListIndex(uint16_t listIndex);
    static bool isLoadEffectListIndex(uint16_t listIndex);
    static uint16_t getLoadEffectListIndex();

  private:
    static void rebuildEffectDisplayNames();

    static bool       s_initialized;
    static bool       s_hasLoadedPlugin;
    static PtfxEntry  s_activeEntry;
    static char*      s_effectDisplayNames[kBuiltInEffectsCount + 2];
    static PtfxHeader s_activeHeader;
};

#endif /* PTFX_MANAGER_H_ */
