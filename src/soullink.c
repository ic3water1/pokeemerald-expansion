#include "soullink.h"

#if __has_include("global.h")
#include "global.h"
#include "main.h"
#include "overworld.h"
#include "battle.h"
#include "battle_util.h"
#include "follower_npc.h"
#include "script_pokemon_util.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "constants/maps.h"
#include "constants/region_map_sections.h"
#include "event_data.h"
#include "palette.h"
#include "script.h"
#else
struct MapHeader {
    u8 regionMapSectionId;
};
extern struct MapHeader gMapHeader;
#endif

// The dedicated EWRAM communication mailbox allocated in .sbss
EWRAM_DATA __attribute__((used)) struct SoulLinkMailbox gSoulLink;

// Monotonic sequence counters for reliable delivery (allocated in .bss, initialized in SoulLink_Init)
static u16 sNextEventSequenceId;
static u16 sNextCmdSequenceId;
static u16 sNextLobbyRequestId;
static u8 sFaintedPartyMask;

#if TESTING
static bool8 sTestSafeOverworldOverrideEnabled;
static bool8 sTestSafeOverworldOverrideValue;
#endif

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
    u32 epoch;

    // Write magic signature for Lua bridge discovery
    gSoulLink.magic = SOULLINK_MAGIC;

    // Protocol version and capabilities
    gSoulLink.protocolVersion = SOULLINK_PROTOCOL_VERSION;
    gSoulLink.capabilities = SOULLINK_CAP_EVENT_QUEUE
                           | SOULLINK_CAP_CMD_QUEUE
                           | SOULLINK_CAP_LOBBY_MANAGEMENT
                           | SOULLINK_CAP_RECONNECT_TOKEN
                           | SOULLINK_CAP_STABLE_ENCOUNTERS;

    // Generate unique non-zero session epoch to isolate restarts
    epoch = gSoulLink.sessionEpoch + 1;
#if __has_include("global.h")
    epoch ^= ((u32)Random() << 16) | (u32)Random();
#endif
    if (epoch == 0)
    {
        epoch = 1;
    }
    gSoulLink.sessionEpoch = epoch;

    gSoulLink.transportState = SOULLINK_TRANSPORT_OFFLINE;
    gSoulLink.mailboxFlags = 0;

    // Reset battle faint event tracker
    SoulLink_ClearFaintEvents();

    // Initialize outgoing event ring buffer
    gSoulLink.eventHead = 0;
    gSoulLink.eventTail = 0;
    gSoulLink.lastAckedEventSequenceId = 0;
    gSoulLink.eventDropCount = 0;
    gSoulLink.criticalOverflowCount = 0;
    memset(gSoulLink.eventQueue, 0, sizeof(gSoulLink.eventQueue));

    // Initialize incoming command ring buffer
    gSoulLink.cmdHead = 0;
    gSoulLink.cmdTail = 0;
    gSoulLink.lastExecutedCmdSequenceId = 0;
    gSoulLink.cmdDropCount = 0;
    memset(gSoulLink.cmdQueue, 0, sizeof(gSoulLink.cmdQueue));

    // Clear lobby request and response
    memset(&gSoulLink.lobbyRequest, 0, sizeof(gSoulLink.lobbyRequest));
    memset(&gSoulLink.lobbyResponse, 0, sizeof(gSoulLink.lobbyResponse));

    // Initial player count and alignment padding
    gSoulLink.playerCount = 1;
    gSoulLink.paddingRoute = 0;

    // Initialize local player (Slot 0)
    for (p = 0; p < MAX_PLAYERS; p++)
    {
        gSoulLink.players[p].isConnected = (p == 0) ? 1 : 0;
        gSoulLink.players[p].isPaused = 0;
        gSoulLink.players[p].badgeCount = 0;
        gSoulLink.players[p].currentMapSec = 0;
        gSoulLink.players[p].isReady = 0;
        gSoulLink.players[p].padding[0] = 0;
        gSoulLink.players[p].padding[1] = 0;
        gSoulLink.players[p].padding[2] = 0;
        for (i = 0; i < PLAYER_NAME_LEN; i++)
        {
            gSoulLink.players[p].name[i] = 0;
        }
    }

    sNextEventSequenceId = 1;
    sNextCmdSequenceId = 1;
    sNextLobbyRequestId = 1;
    sFaintedPartyMask = 0;
#if TESTING
    sTestSafeOverworldOverrideEnabled = FALSE;
    sTestSafeOverworldOverrideValue = TRUE;
#endif
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
        // In multiplayer, unlockedGymStage is authoritatively controlled by the coordinator
        if (!SoulLink_IsMultiplayerActive())
        {
            badges = SoulLink_GetBadgeCount();
            if (badges > gSoulLink.unlockedGymStage)
            {
                gSoulLink.unlockedGymStage = badges;
            }
        }
    }
#endif
}

/**
 * Pushes an event into the outgoing reliable event ring buffer.
 * Supports sequence numbering, critical event overflow protection, and legacy mailbox mirroring.
 */
bool8 SoulLink_PushEvent(u8 eventType, u8 mapSec, u16 species, u8 primaryType, u8 slotIndex, u8 flags)
{
    u16 pending = (u16)(gSoulLink.eventHead - gSoulLink.eventTail);
    u16 slot;
    u16 seq;

    if (eventType == SOULLINK_EVENT_NONE)
    {
        return FALSE;
    }

    // Check if ring buffer is full (16 slots)
    if (pending >= SOULLINK_EVENT_QUEUE_SIZE)
    {
        if (flags & SOULLINK_EVT_FLAG_CRITICAL)
        {
            // CRITICAL EVENT (permanent death, forfeit, catch, badge, wipe):
            // Bounded queue is full and bridge has not acknowledged earlier events.
            // The ROM MUST NOT advance the bridge-owned eventTail!
            // Instead, record critical overflow and flag reconciliation required.
            gSoulLink.criticalOverflowCount++;
            gSoulLink.mailboxFlags |= (SOULLINK_FLAG_CRITICAL_OVERFLOW
                                    | SOULLINK_FLAG_DIRTY_STATE
                                    | SOULLINK_FLAG_RECONCILIATION_REQUIRED);
            return FALSE;
        }
        else
        {
            // Non-critical event (e.g. MAP_CHANGE): Drop and record drop count
            gSoulLink.eventDropCount++;
            gSoulLink.mailboxFlags |= SOULLINK_FLAG_QUEUE_OVERFLOW;
            return FALSE;
        }
    }

    // Allocate sequence ID (monotonic, skipping 0 on 16-bit wraparound)
    if (sNextEventSequenceId == 0)
    {
        sNextEventSequenceId = 1;
    }
    seq = sNextEventSequenceId++;
    if (sNextEventSequenceId == 0)
    {
        sNextEventSequenceId = 1;
    }

    slot = gSoulLink.eventHead & SOULLINK_QUEUE_MASK;

    // Populate event fields atomically before advancing head
    gSoulLink.eventQueue[slot].sequenceId = seq;
    gSoulLink.eventQueue[slot].eventType = eventType;
    gSoulLink.eventQueue[slot].mapSec = mapSec;
    gSoulLink.eventQueue[slot].species = species;
    gSoulLink.eventQueue[slot].primaryType = primaryType;
    gSoulLink.eventQueue[slot].slotIndex = slotIndex;
    gSoulLink.eventQueue[slot].flags = flags;
    gSoulLink.eventQueue[slot].senderPlayerId = 0; // Local player
    gSoulLink.eventQueue[slot].timestamp = 0;

    // Advance write head
    gSoulLink.eventHead++;

    return TRUE;
}

/**
 * Pops the oldest event from the queue. Typically used by test runner or bridge.
 */
bool8 SoulLink_PopEvent(struct SoulLinkEvent *outEvent)
{
    u16 slot;

    if (gSoulLink.eventTail == gSoulLink.eventHead)
    {
        return FALSE; // Queue empty
    }

    slot = gSoulLink.eventTail & SOULLINK_QUEUE_MASK;
    if (outEvent != NULL)
    {
        *outEvent = gSoulLink.eventQueue[slot];
    }

    gSoulLink.lastAckedEventSequenceId = gSoulLink.eventQueue[slot].sequenceId;
    gSoulLink.eventTail++;

    return TRUE;
}

/**
 * Acknowledges events up to sequenceId from the bridge.
 * Advances eventTail ONLY if sequenceId is found within the active pending queue window.
 * Ignores unknown, stale, duplicate, or future sequence IDs to prevent clearing unacknowledged events.
 */
void SoulLink_AcknowledgeEvents(u16 sequenceId)
{
    u16 pending;
    u16 i;
    u16 matchSteps = 0;
    bool8 found = FALSE;

    if (sequenceId == 0)
    {
        return;
    }

    pending = (u16)(gSoulLink.eventHead - gSoulLink.eventTail);
    if (pending == 0)
    {
        return;
    }

    for (i = 0; i < pending; i++)
    {
        u16 slot = (u16)(gSoulLink.eventTail + i) & SOULLINK_QUEUE_MASK;
        if (gSoulLink.eventQueue[slot].sequenceId == sequenceId)
        {
            found = TRUE;
            matchSteps = i + 1;
            break;
        }
    }

    if (!found)
    {
        // Stale, unknown, duplicate, or future ack: ignore, do NOT advance tail!
        return;
    }

    gSoulLink.eventTail += matchSteps;
    gSoulLink.lastAckedEventSequenceId = sequenceId;
}

/**
 * Returns number of unread/pending events currently in the outgoing queue.
 */
u16 SoulLink_GetPendingEventCount(void)
{
    return (u16)(gSoulLink.eventHead - gSoulLink.eventTail);
}

/**
 * Returns total count of dropped non-critical events.
 */
u16 SoulLink_GetEventDropCount(void)
{
    return gSoulLink.eventDropCount;
}

/**
 * Returns total count of critical events that overflowed the bounded buffer.
 */
u16 SoulLink_GetCriticalOverflowCount(void)
{
    return gSoulLink.criticalOverflowCount;
}

/**
 * Emits an event to the outgoing mailbox for the mGBA Lua socket bridge to process.
 * Automatically marks critical events (faint, missed, wipe) so they cannot be dropped on overflow.
 */
void SoulLink_EmitEvent(u8 eventType, u8 mapSec, u16 species, u8 primaryType, u8 slotIndex)
{
    u8 flags = 0;

    if (eventType == SOULLINK_EVENT_FAINTED
     || eventType == SOULLINK_EVENT_MISSED
     || eventType == SOULLINK_EVENT_WIPE
     || eventType == SOULLINK_EVENT_CAUGHT
     || eventType == SOULLINK_EVENT_BADGE)
    {
        flags |= SOULLINK_EVT_FLAG_CRITICAL;
    }

    SoulLink_PushEvent(eventType, mapSec, species, primaryType, slotIndex, flags);
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
 * Returns the recorded encounter ID for a caught Pokémon.
 * Stably reads the per-Pokémon encounter identity stored in its metLocation field.
 */
u16 SoulLink_GetEncounterIdForMon(struct Pokemon *mon)
{
#if __has_include("global.h")
    u8 metLoc = (u8)GetMonData(mon, MON_DATA_MET_LOCATION);
    return metLoc;
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
 * Records a permanent faint event at the exact instant the Pokémon faints in battle.
 * Immediately locks/forfeits the encounter route and emits the death event.
 */
void SoulLink_RecordFaintEvent(u8 partyIndex)
{
    if (partyIndex < PARTY_SIZE)
    {
        sFaintedPartyMask |= (1 << partyIndex);

#if __has_include("global.h")
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][partyIndex];
        u16 encounterId = SoulLink_GetEncounterIdForMon(mon);
        u16 species = GetMonData(mon, MON_DATA_SPECIES);
        u8 primaryType = 0;

        if (species < NUM_SPECIES)
        {
            primaryType = gSpeciesInfo[species].types[0];
        }

        // Immediately mark route as dead upon the actual faint event
        if (encounterId < NUM_MAPSECS)
        {
            gSoulLink.routeState[encounterId] = ROUTE_STATE_DEAD_FORFEITED;
        }

        SoulLink_EmitEvent(SOULLINK_EVENT_FAINTED, (u8)encounterId, species, primaryType, partyIndex);
#endif
    }
}

/**
 * Clears any pending battle faint events so they never carry into subsequent battles.
 */
void SoulLink_ClearFaintEvents(void)
{
    sFaintedPartyMask = 0;
}

/**
 * Processes fainted party members upon return to overworld.
 * Moves fainted Pokémon safely to the Graveyard box (Box 14), marks routes dead, and emits events.
 * Handles both Pokémon with 0 HP and those recorded in sFaintedPartyMask (preventing revive bypass).
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
        bool8 didFaintInBattle = (sFaintedPartyMask & (1 << i)) != 0;

        if (species == SPECIES_NONE)
            continue;

        // Process if either currently at 0 HP OR fainted during the battle (revive attempt)
        if (hp > 0 && !didFaintInBattle)
            continue;

        encounterId = SoulLink_GetEncounterIdForMon(mon);
        primaryType = (species < NUM_SPECIES) ? gSpeciesInfo[species].types[0] : 0;

        // Mark route link as dead
        if (encounterId < NUM_MAPSECS)
        {
            gSoulLink.routeState[encounterId] = ROUTE_STATE_DEAD_FORFEITED;
        }

        // Ensure HP is set to 0 to permanently enforce death even if revived
        u16 zeroHp = 0;
        SetMonData(mon, MON_DATA_HP, &zeroHp);

        // Emit death event if it wasn't already recorded at faint event
        if (!didFaintInBattle)
        {
            SoulLink_EmitEvent(SOULLINK_EVENT_FAINTED, (u8)encounterId, species, primaryType, (u8)i);
        }

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

    sFaintedPartyMask = 0;
#endif
}

/**
 * Safely processes battle-end for NPC-follower partner battles (wild or trainer).
 *
 * In partner multi-battles where the player's team was reduced to 3 chosen mons:
 * 1. Remaps in-battle faint indices (0..2) back to their true playerParty slots (selectedPartyMons[i] - 1).
 * 2. Restores the full 6-mon player party via RestorePartyAfterFollowerNPCBattle().
 * 3. Safely transfers fainted Pokémon to the Graveyard box and compacts the full party.
 * 4. Runs automatic follower healing (HealPlayerParty()) only AFTER deaths are processed,
 *    ensuring that Graveyard Pokémon are never healed and non-participating party mons are never killed.
 */
void SoulLink_HandleFollowerBattlePartyEnd(void)
{
#if __has_include("global.h")
    if (!AreMultiPartiesFullTeams())
    {
        u8 remappedMask = 0;
        u32 i;

        for (i = 0; i < 3; i++)
        {
            u16 origSlot = gSaveBlock2Ptr->frontier.selectedPartyMons[i] - 1;
            if (origSlot < PARTY_SIZE)
            {
                if ((sFaintedPartyMask & (1 << i)) != 0
                 || GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_HP) == 0)
                {
                    remappedMask |= (1 << origSlot);
                }
            }
        }

        RestorePartyAfterFollowerNPCBattle();
        sFaintedPartyMask = remappedMask;
    }

    SoulLink_ProcessFaintedMonsPostBattle();

    if (FNPC_FLAG_HEAL_AFTER_FOLLOWER_BATTLE != 0
     && (FNPC_FLAG_HEAL_AFTER_FOLLOWER_BATTLE == FNPC_ALWAYS
     || FlagGet(FNPC_FLAG_HEAL_AFTER_FOLLOWER_BATTLE)))
    {
        HealPlayerParty();
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
 * Counts total living usable Pokémon currently stored across non-Graveyard PC boxes.
 */
u32 SoulLink_GetLivingBoxPokemonCount(void)
{
    u32 count = 0;
#if __has_include("global.h")
    if (gPokemonStoragePtr != NULL)
    {
        s32 boxId, boxPos;
        for (boxId = 0; boxId < TOTAL_BOXES_COUNT; boxId++)
        {
            if (boxId == SOULLINK_GRAVEYARD_BOX)
                continue;

            for (boxPos = 0; boxPos < IN_BOX_COUNT; boxPos++)
            {
                struct BoxPokemon *boxMon = &gPokemonStoragePtr->boxes[boxId][boxPos];
                u16 species = GetBoxMonData(boxMon, MON_DATA_SPECIES_OR_EGG);
                if (species != SPECIES_NONE && species != SPECIES_EGG)
                {
                    count++;
                }
            }
        }
    }
#endif
    return count;
}

/**
 * Returns total usable Pokémon available to the player across both Party and PC Storage.
 */
u32 SoulLink_GetTotalUsablePokemonCount(void)
{
    return (u32)SoulLink_GetLivingPokemonCount() + SoulLink_GetLivingBoxPokemonCount();
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
 * Returns TRUE if the party has reached the specified gym stage.
 */
bool8 SoulLink_IsGymStageUnlocked(u8 stage)
{
    return (gSoulLink.unlockedGymStage >= stage);
}

/**
 * Script special function: CheckPartyGymUnlock
 * Returns TRUE if the party has reached the required gym stage for the current map, FALSE if blocked.
 * Note: Free of any stale script-variable overrides.
 */
bool8 CheckPartyGymUnlock(void)
{
#if __has_include("global.h")
    return SoulLink_IsGymStageUnlocked(SoulLink_GetRequiredGymStageForMap());
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
 * Checks whether an active multiplayer Soul Link session is currently running.
 * Distinguishes a connected transport from an active multiplayer run:
 * - Connecting the adapter while playing solo does NOT block normal Gym progression.
 * - An active run requires lobbyResponse.runState == SOULLINK_RUN_STATE_IN_PROGRESS.
 * - Loss of transport connection during an active run preserves multiplayer restrictions.
 */
bool8 SoulLink_IsMultiplayerActive(void)
{
    return (gSoulLink.lobbyResponse.runState == SOULLINK_RUN_STATE_IN_PROGRESS);
}

/**
 * Called when a Gym Badge is earned by the player (script special: SoulLink_OnBadgeEarned).
 * In multiplayer mode, group gym progression is authoritatively unlocked by the coordinator
 * via SOULLINK_CMD_UNLOCK_GYM. Do not advance unlockedGymStage locally!
 */
void SoulLink_OnBadgeEarned(void)
{
    u8 badges = SoulLink_GetBadgeCount();
    if (!SoulLink_IsMultiplayerActive())
    {
        if (badges > gSoulLink.unlockedGymStage)
        {
            gSoulLink.unlockedGymStage = badges;
        }
    }
    SoulLink_SaveToSaveBlock3();
    SoulLink_EmitEvent(SOULLINK_EVENT_BADGE, gMapHeader.regionMapSectionId, 0, 0, badges);
}

/**
 * Receives an incoming command from the bridge into the command ring buffer.
 */
bool8 SoulLink_ReceiveCommand(const struct SoulLinkCommand *cmd)
{
    u16 pending;
    u16 slot;

    if (cmd == NULL || cmd->cmdType == SOULLINK_CMD_NONE)
    {
        return FALSE;
    }

    pending = (u16)(gSoulLink.cmdHead - gSoulLink.cmdTail);
    if (pending >= SOULLINK_CMD_QUEUE_SIZE)
    {
        gSoulLink.cmdDropCount++;
        return FALSE;
    }

    slot = gSoulLink.cmdHead & SOULLINK_QUEUE_MASK;
    gSoulLink.cmdQueue[slot] = *cmd;

    // Assign sequence ID if not set by caller
    if (gSoulLink.cmdQueue[slot].sequenceId == 0)
    {
        if (sNextCmdSequenceId == 0)
        {
            sNextCmdSequenceId = 1;
        }
        gSoulLink.cmdQueue[slot].sequenceId = sNextCmdSequenceId++;
        if (sNextCmdSequenceId == 0)
        {
            sNextCmdSequenceId = 1;
        }
    }

    gSoulLink.cmdHead++;
    return TRUE;
}

/**
 * Returns number of pending incoming commands waiting in the command ring buffer.
 */
u16 SoulLink_GetPendingCommandCount(void)
{
    return (u16)(gSoulLink.cmdHead - gSoulLink.cmdTail);
}

#if TESTING
void SoulLink_TestSetSafeOverworldOverride(bool8 enable, bool8 isSafe)
{
    sTestSafeOverworldOverrideEnabled = enable;
    sTestSafeOverworldOverrideValue = isSafe;
}
#endif

/**
 * Checks whether the current game state is genuinely safe for processing incoming overworld commands.
 * Commands must NEVER execute during battles, map fading/warps, active script dialogs, or locked controls.
 */
bool8 SoulLink_IsSafeOverworldState(void)
{
#if TESTING
    if (sTestSafeOverworldOverrideEnabled)
    {
        return sTestSafeOverworldOverrideValue;
    }
#endif

#if __has_include("global.h")
    // Note: gBattleTypeFlags is intentionally NOT checked here because the game engine
    // retains trainer battle flags even after safely returning to the overworld.
    // The true runtime overworld state is verified by ensuring the active main callback
    // is CB2_Overworld, without screen fades, active scripts, or locked controls.
    if (gMain.callback2 != CB2_Overworld)
        return FALSE;
    if (gPaletteFade.active)
        return FALSE;
    if (ScriptContext_IsEnabled())
        return FALSE;
    if (ArePlayerFieldControlsLocked())
        return FALSE;
    return TRUE;
#else
    return TRUE;
#endif
}

/**
 * Checks whether an incoming command sequence is duplicate or stale relative to lastExecuted.
 * Uses modular arithmetic over the non-zero sequence space [1, 65535] (modulus 65535).
 * Returns TRUE if cmdSeq is duplicate or stale, FALSE if fresh.
 */
static bool8 IsCommandSequenceDuplicateOrStale(u16 cmdSeq, u16 lastExecuted)
{
    u32 diff;

    if (cmdSeq == 0)
    {
        return TRUE; // 0 is unassigned/invalid
    }

    if (lastExecuted == 0)
    {
        return FALSE; // Fresh first command of the session
    }

    if (cmdSeq == lastExecuted)
    {
        return TRUE; // Exact duplicate
    }

    // Sequence numbers skip 0 on wraparound, operating over the space [1, 65535] (modulus 65535).
    // Calculate forward modular distance from lastExecuted to cmdSeq.
    diff = (cmdSeq >= lastExecuted)
         ? (u32)(cmdSeq - lastExecuted)
         : (u32)(cmdSeq + 65535 - lastExecuted);

    // If forward distance > 32767, cmdSeq lies in the past half-space (stale)
    if (diff > 32767)
    {
        return TRUE;
    }

    return FALSE; // Fresh forward sequence
}

/**
 * Real ROM enforcement for SOULLINK_CMD_KILL_MON.
 * Identifies the linked Pokémon by stored encounter/link identity across party and PC storage,
 * permanently sets its HP to 0, moves it to the Graveyard box (Box 14), zeroes the source slot,
 * and compacts party slots.
 *
 * Safety & Durability:
 * - Returns SOULLINK_CMD_STATUS_REJECTED if targetMapSec is out of bounds.
 * - Returns SOULLINK_CMD_STATUS_DEFERRED if PC storage is unavailable or Graveyard box is full,
 *   preserving Pokémon data and fainted restrictions without falsely acknowledging enforcement.
 * - Returns SOULLINK_CMD_STATUS_SUCCESS only when required state changes safely complete.
 */
u8 SoulLink_ApplyKillMon(u8 targetMapSec, u16 species)
{
#if __has_include("global.h")
    s32 i;
    bool8 anyPartyMoved = FALSE;
    bool8 transferDeferred = FALSE;

    if (targetMapSec >= NUM_MAPSECS)
        return SOULLINK_CMD_STATUS_REJECTED;

    if (gPokemonStoragePtr == NULL)
        return SOULLINK_CMD_STATUS_DEFERRED;

    // 1. Search player party for Pokémon encountered on targetMapSec
    for (i = (s32)PARTY_SIZE - 1; i >= 0; i--)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        u16 monSpecies = GetMonData(mon, MON_DATA_SPECIES);
        if (monSpecies == SPECIES_NONE || GetMonData(mon, MON_DATA_IS_EGG))
            continue;

        u16 encounterId = SoulLink_GetEncounterIdForMon(mon);
        if (encounterId == targetMapSec)
        {
            u16 zeroHp = 0;
            s16 freeSpot = GetFirstFreeBoxSpot(SOULLINK_GRAVEYARD_BOX);
            if (freeSpot != -1)
            {
                SetMonData(mon, MON_DATA_HP, &zeroHp);
                SetBoxMonAt(SOULLINK_GRAVEYARD_BOX, freeSpot, &mon->box);
                ZeroMonData(mon);
                anyPartyMoved = TRUE;
            }
            else
            {
                // Graveyard box is full: retain in party at 0 HP so data is not lost,
                // and defer command so it is not prematurely acknowledged.
                SetMonData(mon, MON_DATA_HP, &zeroHp);
                transferDeferred = TRUE;
            }
        }
    }

    if (anyPartyMoved)
    {
        CompactPartySlots();
    }

    // 2. Search PC storage boxes (excluding Graveyard box)
    {
        s32 boxId, boxPos;
        for (boxId = 0; boxId < TOTAL_BOXES_COUNT; boxId++)
        {
            if (boxId == SOULLINK_GRAVEYARD_BOX)
                continue;

            for (boxPos = 0; boxPos < IN_BOX_COUNT; boxPos++)
            {
                u16 boxSpecies = GetBoxMonDataAt(boxId, boxPos, MON_DATA_SPECIES);
                if (boxSpecies == SPECIES_NONE || GetBoxMonDataAt(boxId, boxPos, MON_DATA_IS_EGG))
                    continue;

                u8 metLoc = (u8)GetBoxMonDataAt(boxId, boxPos, MON_DATA_MET_LOCATION);
                if (metLoc == targetMapSec)
                {
                    struct BoxPokemon *boxMon = GetBoxedMonPtr(boxId, boxPos);
                    s16 freeSpot = GetFirstFreeBoxSpot(SOULLINK_GRAVEYARD_BOX);
                    if (freeSpot != -1)
                    {
                        SetBoxMonAt(SOULLINK_GRAVEYARD_BOX, freeSpot, boxMon);
                        ZeroBoxMonAt(boxId, boxPos);
                    }
                    else
                    {
                        // Graveyard box full: cannot transfer from PC
                        transferDeferred = TRUE;
                    }
                }
            }
        }
    }

    if (transferDeferred)
    {
        return SOULLINK_CMD_STATUS_DEFERRED;
    }

    // 3. Mark route state as dead and persist to SaveBlock3
    gSoulLink.routeState[targetMapSec] = ROUTE_STATE_DEAD_FORFEITED;
    SoulLink_SaveToSaveBlock3();

    // 4. Check if party is completely wiped of living Pokémon
    if (SoulLink_IsPartyWiped())
    {
        if (SoulLink_GetLivingBoxPokemonCount() > 0)
        {
#if !TESTING
            SetMainCallback2(CB2_WhiteOut);
#endif
        }
        else
        {
            SoulLink_EmitEvent(SOULLINK_EVENT_WIPE, targetMapSec, 0, 0, 0);
#if !TESTING
            SetMainCallback2(CB2_WhiteOut);
#endif
        }
    }

    return SOULLINK_CMD_STATUS_SUCCESS;
#else
    if (targetMapSec < NUM_MAPSECS)
    {
        gSoulLink.routeState[targetMapSec] = ROUTE_STATE_DEAD_FORFEITED;
        return SOULLINK_CMD_STATUS_SUCCESS;
    }
    return SOULLINK_CMD_STATUS_REJECTED;
#endif
}

/**
 * Safe overworld command dispatcher. Called during CB2_Overworld when ScriptContext is idle.
 * Processes incoming synchronized events (linked deaths, route locks, gym unlocks, party wipes).
 * DEFERRED EXECUTION: If not in a genuinely safe overworld state, execution is postponed.
 */
void SoulLink_ProcessOverworldCommands(void)
{
    if (!SoulLink_IsSafeOverworldState())
    {
        return;
    }

    // Drain reliable command ring buffer
    while (gSoulLink.cmdTail != gSoulLink.cmdHead)
    {
        u16 slot = gSoulLink.cmdTail & SOULLINK_QUEUE_MASK;
        struct SoulLinkCommand *cmd = &gSoulLink.cmdQueue[slot];

        // Wrap-safe Replay & Duplicate Protection
        if (IsCommandSequenceDuplicateOrStale(cmd->sequenceId, gSoulLink.lastExecutedCmdSequenceId))
        {
            cmd->executionStatus = SOULLINK_CMD_STATUS_DUPLICATE;
            gSoulLink.cmdTail++;
            continue;
        }

        switch (cmd->cmdType)
        {
        case SOULLINK_CMD_KILL_MON:
            cmd->executionStatus = SoulLink_ApplyKillMon((u8)cmd->targetMapSec, cmd->paramValue);
            break;

        case SOULLINK_CMD_LOCK_ROUTE:
            if (cmd->targetMapSec >= NUM_MAPSECS)
            {
                cmd->executionStatus = SOULLINK_CMD_STATUS_REJECTED;
            }
            else
            {
                gSoulLink.routeState[cmd->targetMapSec] = ROUTE_STATE_DEAD_FORFEITED;
                SoulLink_SaveToSaveBlock3();
                cmd->executionStatus = SOULLINK_CMD_STATUS_SUCCESS;
            }
            break;

        case SOULLINK_CMD_UNLOCK_GYM:
        {
            u8 targetStage = (u8)cmd->paramValue;
            if (targetStage == 0 && cmd->targetMapSec > 0)
            {
                targetStage = cmd->targetMapSec;
            }

            if (targetStage == 0 || targetStage > 8)
            {
                cmd->executionStatus = SOULLINK_CMD_STATUS_REJECTED;
            }
            else
            {
                // Idempotent progression: advancing to targetStage cannot regress or double-count
                if (targetStage > gSoulLink.unlockedGymStage)
                {
                    gSoulLink.unlockedGymStage = targetStage;
                }
                cmd->executionStatus = SOULLINK_CMD_STATUS_SUCCESS;
            }
            break;
        }

        case SOULLINK_CMD_TRIGGER_WIPE:
            // Synchronized game-over screen is deferred to Phase 5
            cmd->executionStatus = SOULLINK_CMD_STATUS_UNIMPLEMENTED;
            break;

        default:
            // Unknown command type: rejected
            cmd->executionStatus = SOULLINK_CMD_STATUS_REJECTED;
            break;
        }

        if (cmd->executionStatus == SOULLINK_CMD_STATUS_SUCCESS)
        {
            gSoulLink.lastExecutedCmdSequenceId = cmd->sequenceId;
        }

        gSoulLink.cmdTail++;
    }
}

/**
 * Sets transport connection state (called by emulator bridge or internal state machine).
 */
void SoulLink_SetTransportState(u8 state)
{
    gSoulLink.transportState = state;
}

/**
 * Returns current transport connection state.
 */
u8 SoulLink_GetTransportState(void)
{
    return gSoulLink.transportState;
}

static u16 AllocateLobbyRequestId(void)
{
    if (sNextLobbyRequestId == 0)
    {
        sNextLobbyRequestId = 1;
    }
    u16 reqId = sNextLobbyRequestId++;
    if (sNextLobbyRequestId == 0)
    {
        sNextLobbyRequestId = 1;
    }
    return reqId;
}

static void SafeCopyBoundedString(u8 *dst, const u8 *src, u32 maxLen)
{
    u32 i;
    bool8 nullFound = FALSE;
    for (i = 0; i < maxLen; i++)
    {
        if (!nullFound && src != NULL && src[i] != 0)
        {
            dst[i] = src[i];
        }
        else
        {
            nullFound = TRUE;
            dst[i] = 0;
        }
    }
    dst[maxLen - 1] = 0;
}

static bool8 IsValidRoomCode(const u8 *code)
{
    u32 i;
    if (code == NULL || code[0] == 0)
    {
        return FALSE;
    }

    // Room codes must fit within SOULLINK_ROOM_CODE_LEN (8 bytes) with a null terminator.
    // Length must be strictly between 1 and SOULLINK_ROOM_CODE_LEN - 1 (1 to 7 alphanumeric characters).
    for (i = 0; i < SOULLINK_ROOM_CODE_LEN - 1; i++)
    {
        u8 c = code[i];
        if (c == 0)
        {
            return (i > 0);
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
        {
            return FALSE; // Invalid character
        }
    }

    // If all previous characters were valid non-zero, index (SOULLINK_ROOM_CODE_LEN - 1) MUST be null
    if (code[SOULLINK_ROOM_CODE_LEN - 1] != 0)
    {
        return FALSE; // String cannot fit without truncation; reject
    }

    return TRUE;
}

/**
 * Returns TRUE if a lobby request is currently pending coordinator response.
 */
bool8 SoulLink_IsLobbyRequestPending(void)
{
    return (gSoulLink.lobbyResponse.responseCode == SOULLINK_RESP_PENDING);
}

/**
 * Initiates an in-game lobby creation request.
 */
bool8 SoulLink_LobbyRequestCreate(u8 maxPlayers)
{
    u16 reqId;

    if (SoulLink_IsLobbyRequestPending())
    {
        return FALSE; // Prevent accidental overwrite while request in-flight
    }

    reqId = AllocateLobbyRequestId();
    gSoulLink.lobbyRequest.requestId = reqId;
    gSoulLink.lobbyRequest.opCode = SOULLINK_LOBBY_OP_CREATE;
    gSoulLink.lobbyRequest.maxPlayers = (maxPlayers >= 2 && maxPlayers <= MAX_PLAYERS) ? maxPlayers : 2;
    SafeCopyBoundedString(gSoulLink.lobbyRequest.roomCode, NULL, SOULLINK_ROOM_CODE_LEN);
    SafeCopyBoundedString(gSoulLink.lobbyRequest.sessionToken, NULL, SOULLINK_SESSION_TOKEN_LEN);

    // Check offline safety: if offline, immediate ERR_NO_BRIDGE response
    if (gSoulLink.transportState == SOULLINK_TRANSPORT_OFFLINE)
    {
        gSoulLink.lobbyResponse.ackRequestId = reqId;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_CREATE;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_ERR_NO_BRIDGE;
    }
    else
    {
        gSoulLink.lobbyResponse.ackRequestId = 0;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_CREATE;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_PENDING;
    }

    return TRUE;
}

/**
 * Initiates an in-game lobby join request with room code.
 */
bool8 SoulLink_LobbyRequestJoin(const u8 *roomCode)
{
    u16 reqId;

    if (SoulLink_IsLobbyRequestPending())
    {
        return FALSE;
    }

    if (!IsValidRoomCode(roomCode))
    {
        return FALSE;
    }

    reqId = AllocateLobbyRequestId();
    gSoulLink.lobbyRequest.requestId = reqId;
    gSoulLink.lobbyRequest.opCode = SOULLINK_LOBBY_OP_JOIN;
    gSoulLink.lobbyRequest.maxPlayers = 0;
    SafeCopyBoundedString(gSoulLink.lobbyRequest.roomCode, roomCode, SOULLINK_ROOM_CODE_LEN);
    SafeCopyBoundedString(gSoulLink.lobbyRequest.sessionToken, NULL, SOULLINK_SESSION_TOKEN_LEN);

    if (gSoulLink.transportState == SOULLINK_TRANSPORT_OFFLINE)
    {
        gSoulLink.lobbyResponse.ackRequestId = reqId;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_JOIN;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_ERR_NO_BRIDGE;
    }
    else
    {
        gSoulLink.lobbyResponse.ackRequestId = 0;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_JOIN;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_PENDING;
    }

    return TRUE;
}

/**
 * Signals ready or unready status in lobby.
 */
bool8 SoulLink_LobbyRequestReady(bool8 ready)
{
    u16 reqId;

    if (SoulLink_IsLobbyRequestPending())
    {
        return FALSE;
    }

    reqId = AllocateLobbyRequestId();
    gSoulLink.lobbyRequest.requestId = reqId;
    gSoulLink.lobbyRequest.opCode = ready ? SOULLINK_LOBBY_OP_READY : SOULLINK_LOBBY_OP_UNREADY;

    if (gSoulLink.transportState == SOULLINK_TRANSPORT_OFFLINE)
    {
        gSoulLink.lobbyResponse.ackRequestId = reqId;
        gSoulLink.lobbyResponse.opCode = gSoulLink.lobbyRequest.opCode;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_ERR_NO_BRIDGE;
    }
    else
    {
        gSoulLink.lobbyResponse.ackRequestId = 0;
        gSoulLink.lobbyResponse.opCode = gSoulLink.lobbyRequest.opCode;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_PENDING;
    }

    return TRUE;
}

/**
 * Host initiates synchronized run start.
 */
bool8 SoulLink_LobbyRequestStartRun(void)
{
    u16 reqId;

    if (SoulLink_IsLobbyRequestPending())
    {
        return FALSE;
    }

    reqId = AllocateLobbyRequestId();
    gSoulLink.lobbyRequest.requestId = reqId;
    gSoulLink.lobbyRequest.opCode = SOULLINK_LOBBY_OP_START_RUN;

    if (gSoulLink.transportState == SOULLINK_TRANSPORT_OFFLINE)
    {
        gSoulLink.lobbyResponse.ackRequestId = reqId;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_START_RUN;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_ERR_NO_BRIDGE;
    }
    else
    {
        gSoulLink.lobbyResponse.ackRequestId = 0;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_START_RUN;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_PENDING;
    }

    return TRUE;
}

/**
 * Signals leaving the lobby.
 */
bool8 SoulLink_LobbyRequestLeave(void)
{
    u16 reqId;

    if (SoulLink_IsLobbyRequestPending())
    {
        return FALSE;
    }

    reqId = AllocateLobbyRequestId();
    gSoulLink.lobbyRequest.requestId = reqId;
    gSoulLink.lobbyRequest.opCode = SOULLINK_LOBBY_OP_LEAVE;

    if (gSoulLink.transportState == SOULLINK_TRANSPORT_OFFLINE)
    {
        gSoulLink.lobbyResponse.ackRequestId = reqId;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_LEAVE;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_ERR_NO_BRIDGE;
    }
    else
    {
        gSoulLink.lobbyResponse.ackRequestId = 0;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_LEAVE;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_PENDING;
    }

    return TRUE;
}

/**
 * Reconnects using existing room code and session token.
 */
bool8 SoulLink_LobbyRequestReconnect(const u8 *roomCode, const u8 *sessionToken)
{
    u16 reqId;

    if (SoulLink_IsLobbyRequestPending())
    {
        return FALSE;
    }

    if (!IsValidRoomCode(roomCode))
    {
        return FALSE;
    }

    reqId = AllocateLobbyRequestId();
    gSoulLink.lobbyRequest.requestId = reqId;
    gSoulLink.lobbyRequest.opCode = SOULLINK_LOBBY_OP_RECONNECT;
    SafeCopyBoundedString(gSoulLink.lobbyRequest.roomCode, roomCode, SOULLINK_ROOM_CODE_LEN);
    SafeCopyBoundedString(gSoulLink.lobbyRequest.sessionToken, sessionToken, SOULLINK_SESSION_TOKEN_LEN);

    if (gSoulLink.transportState == SOULLINK_TRANSPORT_OFFLINE)
    {
        gSoulLink.lobbyResponse.ackRequestId = reqId;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_RECONNECT;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_ERR_NO_BRIDGE;
    }
    else
    {
        gSoulLink.lobbyResponse.ackRequestId = 0;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_RECONNECT;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_PENDING;
    }

    return TRUE;
}

/**
 * Queries current lobby and player status.
 */
bool8 SoulLink_LobbyRequestGetStatus(void)
{
    u16 reqId;

    if (SoulLink_IsLobbyRequestPending())
    {
        return FALSE;
    }

    reqId = AllocateLobbyRequestId();
    gSoulLink.lobbyRequest.requestId = reqId;
    gSoulLink.lobbyRequest.opCode = SOULLINK_LOBBY_OP_GET_STATUS;

    if (gSoulLink.transportState == SOULLINK_TRANSPORT_OFFLINE)
    {
        gSoulLink.lobbyResponse.ackRequestId = reqId;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_GET_STATUS;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_ERR_NO_BRIDGE;
    }
    else
    {
        gSoulLink.lobbyResponse.ackRequestId = 0;
        gSoulLink.lobbyResponse.opCode = SOULLINK_LOBBY_OP_GET_STATUS;
        gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_PENDING;
    }

    return TRUE;
}

#if TESTING
void SoulLink_TestSetNextEventSequenceId(u16 seq)
{
    sNextEventSequenceId = seq;
}

void SoulLink_TestSetSessionEpoch(u32 epoch)
{
    gSoulLink.sessionEpoch = epoch;
}
#endif
