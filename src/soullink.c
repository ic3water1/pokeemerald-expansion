#include "soullink.h"

#if __has_include("global.h")
#include "global.h"
#include "overworld.h"
#include "battle.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "constants/maps.h"
#include "constants/region_map_sections.h"
#include "event_data.h"
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
 * Initializes the Soul Link mailbox structure in EWRAM on GBA boot.
 * Sets the 0x4C554F53 ("SOUL") magic header so the mGBA Lua bridge can locate it.
 * Note: Does NOT clear route state or gym progression so saved games are preserved.
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

    // Initial player count
    gSoulLink.playerCount = 1;

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
 * Initializes fresh Nuzlocke state when starting a New Game.
 */
void SoulLink_NewGame(void)
{
    u32 i, p;

    gSoulLink.unlockedGymStage = 0;

    for (i = 0; i < NUM_MAPSECS; i++)
    {
        gSoulLink.routeState[i] = ROUTE_STATE_OPEN;
        gSoulLink.routeTypesMask[i] = 0;
        for (p = 0; p < MAX_PLAYERS; p++)
        {
            gSoulLink.routeSpecies[i][p] = 0;
        }
    }

    SoulLink_SaveToSaveBlock3();
}

/**
 * Saves persistent SoulLink data into SaveBlock3 (which is flushed to flash save sectors).
 */
void SoulLink_SaveToSaveBlock3(void)
{
#if __has_include("global.h")
    if (gSaveBlock3Ptr != NULL)
    {
        u32 i;
        gSaveBlock3Ptr->soulLink.unlockedGymStage = gSoulLink.unlockedGymStage;
        for (i = 0; i < NUM_MAPSECS; i++)
        {
            gSaveBlock3Ptr->soulLink.routeState[i] = gSoulLink.routeState[i];
            gSaveBlock3Ptr->soulLink.routeSpecies[i] = gSoulLink.routeSpecies[i][0];
            gSaveBlock3Ptr->soulLink.routeTypesMask[i] = gSoulLink.routeTypesMask[i];
        }
    }
#endif
}

/**
 * Loads persistent SoulLink data from SaveBlock3 into EWRAM mailbox.
 */
void SoulLink_LoadFromSaveBlock3(void)
{
#if __has_include("global.h")
    if (gSaveBlock3Ptr != NULL)
    {
        u32 i;
        u8 badges;

        gSoulLink.unlockedGymStage = gSaveBlock3Ptr->soulLink.unlockedGymStage;
        for (i = 0; i < NUM_MAPSECS; i++)
        {
            gSoulLink.routeState[i] = gSaveBlock3Ptr->soulLink.routeState[i];
            gSoulLink.routeSpecies[i][0] = gSaveBlock3Ptr->soulLink.routeSpecies[i];
            gSoulLink.routeTypesMask[i] = gSaveBlock3Ptr->soulLink.routeTypesMask[i];
        }

        // Synchronize unlocked gym stage with in-game badge count if badges exceed saved stage
        badges = SoulLink_GetBadgeCount();
        if (badges > gSoulLink.unlockedGymStage)
        {
            gSoulLink.unlockedGymStage = badges;
        }
    }
#endif
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
 * Returns a stable encounter identifier. Handles split routes like Route 104 North vs South.
 */
u16 SoulLink_GetEncounterId(void)
{
    u8 mapSec = gMapHeader.regionMapSectionId;

#if __has_include("global.h")
    if (mapSec == MAPSEC_ROUTE_104)
    {
        // Route 104 is split into North and South by Petalburg Woods
        if (gSaveBlock1Ptr != NULL && gSaveBlock1Ptr->pos.y < 35)
        {
            return ENCOUNTER_ID_ROUTE_104_NORTH;
        }
        return ENCOUNTER_ID_ROUTE_104_SOUTH;
    }
#endif

    return (u16)mapSec;
}

/**
 * Returns the recorded encounter ID for a caught Pokémon, disambiguating split routes.
 */
u16 SoulLink_GetEncounterIdForMon(struct Pokemon *mon)
{
#if __has_include("global.h")
    u8 metLoc = (u8)GetMonData(mon, MON_DATA_MET_LOCATION);
    u16 species = GetMonData(mon, MON_DATA_SPECIES);

    if (metLoc == MAPSEC_ROUTE_104)
    {
        // If Route 104 North was registered with this species, attribute death to Route 104 North
        if (gSoulLink.routeSpecies[ENCOUNTER_ID_ROUTE_104_NORTH][0] == species)
        {
            return ENCOUNTER_ID_ROUTE_104_NORTH;
        }
        return ENCOUNTER_ID_ROUTE_104_SOUTH;
    }

    return (u16)metLoc;
#else
    return 0;
#endif
}

/**
 * Checks whether wild catches are permitted on the current map sector according to the Soul Link rules.
 * Returns TRUE if the route is open/available (or active during current battle), or FALSE if already caught or forfeited.
 */
bool8 SoulLink_CanCatchOnCurrentMap(void)
{
    u16 encounterId = SoulLink_GetEncounterId();

    if (encounterId >= NUM_MAPSECS)
    {
        return FALSE;
    }

    // Route must be OPEN or the encounter currently in progress
    return (gSoulLink.routeState[encounterId] == ROUTE_STATE_OPEN
         || gSoulLink.routeState[encounterId] == ROUTE_STATE_ENCOUNTER_ACTIVE);
}

/**
 * Called when a wild battle initiates. Marks the route as having an active encounter in progress.
 */
void SoulLink_OnWildBattleStart(void)
{
    u16 encounterId = SoulLink_GetEncounterId();

    if (encounterId < NUM_MAPSECS && gSoulLink.routeState[encounterId] == ROUTE_STATE_OPEN)
    {
        gSoulLink.routeState[encounterId] = ROUTE_STATE_ENCOUNTER_ACTIVE;
    }
}

/**
 * Called when the player successfully catches a wild Pokémon.
 */
void SoulLink_OnWildCatchSuccess(u16 species)
{
    u16 encounterId = SoulLink_GetEncounterId();
    u8 primaryType = 0;

#if __has_include("global.h")
    if (species < NUM_SPECIES)
    {
        primaryType = gSpeciesInfo[species].types[0];
    }
#endif

    if (encounterId < NUM_MAPSECS)
    {
        // Enforce catch validation guard: only allow catch if route was OPEN or ACTIVE
        if (gSoulLink.routeState[encounterId] == ROUTE_STATE_OPEN
         || gSoulLink.routeState[encounterId] == ROUTE_STATE_ENCOUNTER_ACTIVE)
        {
            gSoulLink.routeState[encounterId] = ROUTE_STATE_ALIVE;
            gSoulLink.routeSpecies[encounterId][0] = species;
            gSoulLink.routeTypesMask[encounterId] |= SOULLINK_TYPE_BIT(primaryType);
            SoulLink_EmitEvent(SOULLINK_EVENT_CAUGHT, (u8)encounterId, species, primaryType, 0);
        }
    }
}

/**
 * Called when the player flees from or faints the wild encounter without catching it.
 */
void SoulLink_OnWildEncounterFailed(void)
{
    u16 encounterId = SoulLink_GetEncounterId();

    if (encounterId < NUM_MAPSECS && gSoulLink.routeState[encounterId] == ROUTE_STATE_ENCOUNTER_ACTIVE)
    {
        gSoulLink.routeState[encounterId] = ROUTE_STATE_DEAD_FORFEITED;
        SoulLink_EmitEvent(SOULLINK_EVENT_MISSED, (u8)encounterId, 0, 0, 0);
    }
}

/**
 * Processes fainted party members upon return to overworld.
 * Moves fainted Pokémon safely to the Graveyard box (Box 14), marks routes dead, and emits events.
 * Crucially: Never zeroes or deletes a Pokémon unless it has been safely stored in the Graveyard box.
 */
void SoulLink_ProcessFaintedMonsPostBattle(void)
{
#if __has_include("global.h")
    s32 i;
    bool8 anyMoved = FALSE;

    for (i = (s32)PARTY_SIZE - 1; i >= 0; i--)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        u16 species = GetMonData(mon, MON_DATA_SPECIES);
        u16 hp = GetMonData(mon, MON_DATA_HP);
        u16 encounterId;
        u8 primaryType;
        bool8 storedInGraveyard = FALSE;

        if (species == SPECIES_NONE || hp > 0)
            continue;

        encounterId = SoulLink_GetEncounterIdForMon(mon);
        primaryType = gSpeciesInfo[species].types[0];

        // Mark route link as dead
        if (encounterId < NUM_MAPSECS)
        {
            gSoulLink.routeState[encounterId] = ROUTE_STATE_DEAD_FORFEITED;
        }

        // Emit death event
        SoulLink_EmitEvent(SOULLINK_EVENT_FAINTED, (u8)encounterId, species, primaryType, (u8)i);

        // Attempt transfer to Graveyard PC Box (Box 13)
        if (gPokemonStoragePtr != NULL)
        {
            s16 freeSpot = GetFirstFreeBoxSpot(SOULLINK_GRAVEYARD_BOX);
            if (freeSpot != -1)
            {
                SetBoxMonAt(SOULLINK_GRAVEYARD_BOX, freeSpot, &mon->box);
                storedInGraveyard = TRUE;
            }
        }

        // Only delete from party if safely placed in the Graveyard box!
        if (storedInGraveyard)
        {
            ZeroMonData(mon);
            anyMoved = TRUE;
        }
        else
        {
            // Graveyard full or PC unavailable: keep mon in party (at 0 HP)
            // so Pokémon data is NEVER permanently lost.
        }
    }

    if (anyMoved)
    {
        CompactPartySlots();
    }
#endif
}

/**
 * Checks if the player has 0 usable Pokémon remaining across their party.
 */
bool8 SoulLink_IsPartyWiped(void)
{
#if __has_include("global.h")
    u32 i;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        u16 species = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SPECIES);
        u16 hp = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_HP);
        if (species != SPECIES_NONE && hp > 0)
        {
            return FALSE;
        }
    }
    return TRUE;
#else
    return FALSE;
#endif
}

/**
 * Counts total living usable Pokémon currently in the player party.
 */
u8 SoulLink_GetLivingPokemonCount(void)
{
    u8 count = 0;
#if __has_include("global.h")
    u32 i;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        u16 species = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SPECIES);
        u16 hp = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_HP);
        if (species != SPECIES_NONE && hp > 0)
        {
            count++;
        }
    }
#endif
    return count;
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
 * Returns the required unlockedGymStage for the current map.
 */
u8 SoulLink_GetRequiredGymStageForMap(void)
{
#if __has_include("global.h")
    u16 mapGroup = gSaveBlock1Ptr->location.mapGroup;
    u16 mapNum = gSaveBlock1Ptr->location.mapNum;
    u16 mapConstant = (mapNum | (mapGroup << 8));

    switch (mapConstant)
    {
    case MAP_RUSTBORO_CITY_GYM:
        return 0; // Gym 1 (Roxanne)
    case MAP_DEWFORD_TOWN_GYM:
        return 1; // Gym 2 (Brawly)
    case MAP_MAUVILLE_CITY_GYM:
        return 2; // Gym 3 (Wattson)
    case MAP_LAVARIDGE_TOWN_GYM_1F:
    case MAP_LAVARIDGE_TOWN_GYM_B1F:
        return 3; // Gym 4 (Flannery)
    case MAP_PETALBURG_CITY_GYM:
        return 4; // Gym 5 (Norman)
    case MAP_FORTREE_CITY_GYM:
        return 5; // Gym 6 (Winona)
    case MAP_MOSSDEEP_CITY_GYM:
        return 6; // Gym 7 (Tate & Liza)
    case MAP_SOOTOPOLIS_CITY_GYM_1F:
    case MAP_SOOTOPOLIS_CITY_GYM_B1F:
        return 7; // Gym 8 (Juan)
    default:
        return 0;
    }
#else
    return 0;
#endif
}

/**
 * Script special function: CheckPartyGymUnlock
 * Returns TRUE if the party has reached the required gym stage, FALSE if blocked.
 */
bool8 CheckPartyGymUnlock(void)
{
#if __has_include("global.h")
    u8 req = SoulLink_GetRequiredGymStageForMap();
    if (gSpecialVar_0x8004 != 0)
    {
        req = (u8)(gSpecialVar_0x8004 - 1);
    }
    return (gSoulLink.unlockedGymStage >= req);
#else
    return TRUE;
#endif
}

/**
 * Counts total badges currently earned by the player.
 */
u8 SoulLink_GetBadgeCount(void)
{
    u8 count = 0;
#if __has_include("global.h")
    u32 i;
    for (i = FLAG_BADGE01_GET; i < FLAG_BADGE01_GET + NUM_BADGES; i++)
    {
        if (FlagGet(i))
            count++;
    }
#endif
    return count;
}

/**
 * Called when a Gym Badge is earned by the player (script special: SoulLink_OnBadgeEarned).
 */
void SoulLink_OnBadgeEarned(void)
{
    u8 badges = SoulLink_GetBadgeCount();
    if (badges > gSoulLink.unlockedGymStage)
    {
        gSoulLink.unlockedGymStage = badges;
    }
    SoulLink_SaveToSaveBlock3();
    SoulLink_EmitEvent(SOULLINK_EVENT_BADGE, gMapHeader.regionMapSectionId, 0, 0, badges);
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
        // Linked partner's Pokémon fainted: Mark route dead
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
