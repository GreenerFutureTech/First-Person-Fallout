#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "art.h"
#include "color.h"
#include "config.h"
#include "dbox.h"
#include "debug.h"
#include "draw.h"
#include "game.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "graph_lib.h"
#include "input.h"
#include "item.h"
#include "kb.h"
#include "map.h"
#include "memory.h"
#include "object.h"
#include "platform_compat.h"
#include "settings.h"
#include "svga.h"
#include "text_font.h"
#include "window_manager.h"
#include "worldmap.h"
#include "automap.h"

#include "db.h"

namespace fallout {

// The automap display flags, split out of the client's automap.cc rendering TU
// on the game_movie_state.cc precedent: move the state, not the projector.
//
// The flags round-trip through the savegame, so the driver in savegame.cc has
// to reach them. The map data itself already lives in AUTOMAP.DB and is written
// by automapSaveCurrent(); this is only the small header the save file carries.

// 0x41B884
AutomapFlags gAutomapFlags = AUTOMAP_NONE;

// 0x41B87C
int automapLoad(File* stream)
{
    return fileReadInt32Enum<AutomapFlags>(stream, &gAutomapFlags);
}

// 0x41B898
int automapSave(File* stream)
{
    return fileWriteInt32Enum<AutomapFlags>(stream, gAutomapFlags);
}

} // namespace fallout
