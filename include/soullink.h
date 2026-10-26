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
    ROUTE_STATE_OPEN = 0,
    ROUTE_STATE_ALIVE = 1,
    ROUTE_STATE_BOXED = 2,
    ROUTE_STATE_DEAD_FORFEITED = 3,
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
    u8 unlockedGymStage;                        // Highest gym currently allowed
    u8 routeState[NUM_MAPSECS];                 // 0 = Open, 1 = Alive, 2 = Boxed, 3 = Dead/Forfeited
    u16 routeSpecies[NUM_MAPSECS][MAX_PLAYERS]; // Species caught per route per player for Tracker UI
    u8 routeTypesMask[NUM_MAPSECS];             // Bitmask of primary types used within each route link
    struct PlayerSyncInfo players[MAX_PLAYERS]; // Live status, badges, and current route of all 8 players
};

extern EWRAM_DATA struct SoulLinkMailbox gSoulLink;

/* Function Declarations */
void SoulLink_Init(void);
void SoulLink_EmitEvent(u8 eventType, u8 mapSec, u16 species, u8 primaryType, u8 slotIndex);
bool8 SoulLink_CanCatchOnCurrentMap(void);
u8 SoulLink_GetCurrentLevelCap(void);
void SoulLink_ProcessOverworldCommands(void);

#endif // GUARD_SOULLINK_H
