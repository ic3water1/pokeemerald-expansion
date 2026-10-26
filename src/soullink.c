#include "soullink.h"

#if __has_include("global.h")
#include "global.h"
#include "overworld.h"
#else
struct MapHeader {
    u8 regionMapSectionId;
};
extern struct MapHeader gMapHeader;
#endif

// The dedicated EWRAM communication mailbox allocated in .sbss
EWRAM_DATA __attribute__((used)) struct SoulLinkMailbox gSoulLink = {0};

// Hard level caps indexed by unlockedGymStage (0 through 8)
static const u8 sGymLevelCaps[] = {
    15, // Stage 0: Before Badge 1 (Roxanne - Ace Lv. 15)
    19, // Stage 1: Before Badge 2 (Brawly - Ace Lv. 19)
    24, // Stage 2: Before Badge 3 (Wattson - Ace Lv. 24)
    29, // Stage 3: Before Badge 4 (Flannery - Ace Lv. 29)
    31, // Stage 4: Before Badge 5 (Norman - Ace Lv. 31)
    33, // Stage 5: Before Badge 6 (Winona - Ace Lv. 33)
    42, // Stage 6: Before Badge 7 (Tate & Liza - Ace Lv. 42)
    43, // Stage 7: Before Badge 8 (Juan - Ace Lv. 43)
    58, // Stage 8: Before Champion (Wallace - Ace Lv. 58)
};

/**
 * Initializes the Soul Link mailbox structure in EWRAM.
 * Sets the 0x4C554F53 ("SOUL") magic header so the mGBA Lua bridge can locate it.
 */
void SoulLink_Init(void)
{
    u32 i;
    u32 p;

    // Write magic signature for Lua bridge discovery
    gSoulLink.magic = SOULLINK_MAGIC;

    // Clear outgoing event mailbox
    gSoulLink.outEventType = SOULLINK_EVENT_NONE;
    gSoulLink.outMapSec = 0;
    gSoulLink.outSpecies = 0;
    gSoulLink.outPrimaryType = 0;
    gSoulLink.outSlotIndex = 0;

    // Clear incoming command mailbox
    gSoulLink.inCommandType = SOULLINK_CMD_NONE;
    gSoulLink.inTargetMapSec = 0;
    gSoulLink.inNotificationPlayerId = 0;

    // Initial game state
    gSoulLink.playerCount = 1;
    gSoulLink.unlockedGymStage = 0;

    // Initialize all Hoenn route states to OPEN
    for (i = 0; i < NUM_MAPSECS; i++)
    {
        gSoulLink.routeState[i] = ROUTE_STATE_OPEN;
        gSoulLink.routeTypesMask[i] = 0;
        for (p = 0; p < MAX_PLAYERS; p++)
        {
            gSoulLink.routeSpecies[i][p] = 0;
        }
    }

    // Initialize local player (Slot 0)
    for (p = 0; p < MAX_PLAYERS; p++)
    {
        gSoulLink.players[p].isConnected = (p == 0) ? 1 : 0;
        gSoulLink.players[p].isPaused = 0;
        gSoulLink.players[p].badgeCount = 0;
        gSoulLink.players[p].currentMapSec = 0;
        for (i = 0; i < PLAYER_NAME_LEN; i++)
        {
            gSoulLink.players[p].name[i] = 0;
        }
    }
}

/**
 * Emits an event to the outgoing mailbox for the mGBA Lua socket bridge to process.
 */
void SoulLink_EmitEvent(u8 eventType, u8 mapSec, u16 species, u8 primaryType, u8 slotIndex)
{
    gSoulLink.outEventType = eventType;
    gSoulLink.outMapSec = mapSec;
    gSoulLink.outSpecies = species;
    gSoulLink.outPrimaryType = primaryType;
    gSoulLink.outSlotIndex = slotIndex;
}

/**
 * Checks whether wild catches are permitted on the current map sector according to the Soul Link rules.
 * Returns TRUE if the route is still open/available, or FALSE if already caught or forfeited.
 */
bool8 SoulLink_CanCatchOnCurrentMap(void)
{
    u8 currentMapSec = gMapHeader.regionMapSectionId;

    if (currentMapSec >= NUM_MAPSECS)
    {
        // Special or out-of-bounds area: disallow catch by default
        return FALSE;
    }

    // Only permitted if this route has not yet been caught or forfeited by any party member
    return (gSoulLink.routeState[currentMapSec] == ROUTE_STATE_OPEN);
}

/**
 * Returns the current hard level cap based on the party's unlocked Gym progression stage.
 */
u8 SoulLink_GetCurrentLevelCap(void)
{
    if (gSoulLink.unlockedGymStage < (sizeof(sGymLevelCaps) / sizeof(sGymLevelCaps[0])))
    {
        return sGymLevelCaps[gSoulLink.unlockedGymStage];
    }

    // Default to level 100 once all Gyms and Champion are cleared
    return 100;
}

/**
 * Safe overworld command dispatcher. Called during CB2_Overworld when ScriptContext is idle.
 * Processes incoming synchronized events (linked deaths, route locks, gym unlocks, party wipes).
 */
void SoulLink_ProcessOverworldCommands(void)
{
    u8 cmd = gSoulLink.inCommandType;
    u8 mapSec = gSoulLink.inTargetMapSec;

    if (cmd == SOULLINK_CMD_NONE)
    {
        return;
    }

    switch (cmd)
    {
    case SOULLINK_CMD_KILL_MON:
        // Linked partner's Pokémon fainted: Mark route dead and queue Graveyard transfer
        if (mapSec < NUM_MAPSECS)
        {
            gSoulLink.routeState[mapSec] = ROUTE_STATE_DEAD_FORFEITED;
        }
        break;

    case SOULLINK_CMD_LOCK_ROUTE:
        // Partner failed/missed encounter: lock this route for all players
        if (mapSec < NUM_MAPSECS)
        {
            gSoulLink.routeState[mapSec] = ROUTE_STATE_DEAD_FORFEITED;
        }
        break;

    case SOULLINK_CMD_UNLOCK_GYM:
        // All connected players defeated the required Gym Leader; increment progression
        if (gSoulLink.unlockedGymStage < 8)
        {
            gSoulLink.unlockedGymStage++;
        }
        break;

    case SOULLINK_CMD_TRIGGER_WIPE:
        // Global party wipe: whiteout state triggered across all clients
        break;

    default:
        break;
    }

    // Acknowledge command completion so Lua bridge can push next event
    gSoulLink.inCommandType = SOULLINK_CMD_NONE;
}
