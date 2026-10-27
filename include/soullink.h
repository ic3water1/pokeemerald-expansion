#ifndef GUARD_SOULLINK_H
#define GUARD_SOULLINK_H

#if __has_include("global.h")
#include "global.h"
#else
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef uint8_t bool8;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef EWRAM_DATA
#define EWRAM_DATA __attribute__((section(".sbss")))
#endif
#endif

// Magic header for mGBA Lua bridge detection in EWRAM ("SOUL" in ASCII little-endian)
#define SOULLINK_MAGIC 0x4C554F53

#define MAX_PLAYERS 8
#define NUM_MAPSECS 213 // Total trackable route/location IDs in Hoenn
#define PLAYER_NAME_LEN 8

// Graveyard Box: Box 14 (0-indexed 13)
#define SOULLINK_GRAVEYARD_BOX 13

// Bitmask helper for Pokémon types (supports up to 32 types, covering all 20 types)
#define SOULLINK_TYPE_BIT(type) ((u32)1 << (type))

// Special Stable Encounter IDs mapped into unused slots of routeState[213]
#define ENCOUNTER_ID_ROUTE_104_SOUTH 28 // Standard MAPSEC_ROUTE_104
#define ENCOUNTER_ID_ROUTE_104_NORTH 210
#define ENCOUNTER_ID_STATIC_BIRCH_STARTER 211
#define ENCOUNTER_ID_STATIC_CASTFORM 212

// Outgoing Event Types (GBA -> Lua bridge)
enum SoulLinkEventType {
    SOULLINK_EVENT_NONE = 0,
    SOULLINK_EVENT_CAUGHT = 1,
    SOULLINK_EVENT_MISSED = 2,
    SOULLINK_EVENT_FAINTED = 3,
    SOULLINK_EVENT_BADGE = 4,
    SOULLINK_EVENT_WIPE = 5,
    SOULLINK_EVENT_MAP_CHANGE = 6,
};

// Incoming Command Types (Lua bridge -> GBA)
enum SoulLinkCommandType {
    SOULLINK_CMD_NONE = 0,
    SOULLINK_CMD_KILL_MON = 1,
    SOULLINK_CMD_LOCK_ROUTE = 2,
    SOULLINK_CMD_UNLOCK_GYM = 3,
    SOULLINK_CMD_TRIGGER_WIPE = 4,
};

// Route status in Soul Link matrix
enum SoulLinkRouteState {
    ROUTE_STATE_OPEN = 0,             // Not yet encountered
    ROUTE_STATE_ALIVE = 1,            // Caught and currently alive
    ROUTE_STATE_BOXED = 2,            // Boxed in PC storage
    ROUTE_STATE_DEAD_FORFEITED = 3,   // Fainted, fled, or encounter forfeited
    ROUTE_STATE_ENCOUNTER_ACTIVE = 4, // Currently in first encounter battle
};

struct PlayerSyncInfo {
    u8 isConnected;
    u8 isPaused;
    u8 badgeCount;
    u8 currentMapSec;
    u8 name[PLAYER_NAME_LEN];
};

struct SoulLinkMailbox {
    // Magic header at offset 0 for mGBA Lua bridge auto-detection
    u32 magic;

    // Outgoing from GBA -> Lua (Cleared by Lua after reading)
    u8 outEventType;           // 0 = None, 1 = Caught, 2 = Missed, 3 = Fainted, 4 = Badge, 5 = Wipe, 6 = MapChange
    u8 outMapSec;              // Route/Area ID (MAPSEC_*) where catch/miss/move occurred
    u16 outSpecies;            // Species caught
    u8 outPrimaryType;         // Primary type of caught mon
    u8 outSlotIndex;           // Party/Box link ID

    // Incoming from Lua -> GBA (Processed by GBA outside of battle)
    u8 inCommandType;          // 0 = None, 1 = Kill Linked Mon, 2 = Lock Route, 3 = Unlock Gym, 4 = Trigger Wipe
    u8 inTargetMapSec;         // Route ID to kill/lock
    u8 inNotificationPlayerId; // Which friend triggered the event (for UI popup)

    // Persistent Synced State
    u8 playerCount;                             // 1 to 8
    u8 unlockedGymStage;                        // Highest gym currently allowed (0 to 8)
    u8 routeState[NUM_MAPSECS];                 // 0 = Open, 1 = Alive, 2 = Boxed, 3 = Dead/Forfeited, 4 = Active
    u16 routeSpecies[NUM_MAPSECS][MAX_PLAYERS]; // Species caught per route per player for Tracker UI
    u32 routeTypesMask[NUM_MAPSECS];            // 32-bit bitmask of primary types used within each route link
    struct PlayerSyncInfo players[MAX_PLAYERS]; // Live status, badges, and current route of all 8 players
};

struct Pokemon;

extern EWRAM_DATA struct SoulLinkMailbox gSoulLink;

/* Function Declarations */
void SoulLink_Init(void);
void SoulLink_NewGame(void);
void SoulLink_SaveToSaveBlock3(void);
void SoulLink_LoadFromSaveBlock3(void);
void SoulLink_EmitEvent(u8 eventType, u8 mapSec, u16 species, u8 primaryType, u8 slotIndex);
u16 SoulLink_GetEncounterId(void);
u16 SoulLink_GetEncounterIdForMon(struct Pokemon *mon);
bool8 SoulLink_CanCatchOnCurrentMap(void);
u8 SoulLink_GetCurrentLevelCap(void);
void SoulLink_ProcessOverworldCommands(void);

// Phase 2: Local Nuzlocke Mechanics
void SoulLink_OnWildBattleStart(void);
void SoulLink_OnWildCatchSuccess(u16 species);
void SoulLink_OnWildEncounterFailed(void);
void SoulLink_ProcessFaintedMonsPostBattle(void);
bool8 SoulLink_IsPartyWiped(void);
u8 SoulLink_GetLivingPokemonCount(void);

// Gym progression
bool8 CheckPartyGymUnlock(void);
u8 SoulLink_GetRequiredGymStageForMap(void);
u8 SoulLink_GetBadgeCount(void);
void SoulLink_OnBadgeEarned(void);

#endif // GUARD_SOULLINK_H
