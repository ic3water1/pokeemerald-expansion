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

// Protocol version (Phase 3 Step 1: Versioned Mailbox)
#define SOULLINK_PROTOCOL_VERSION 1

// Capability flags (bitmask in mailbox.capabilities)
#define SOULLINK_CAP_EVENT_QUEUE       (1 << 0)
#define SOULLINK_CAP_CMD_QUEUE         (1 << 1)
#define SOULLINK_CAP_LOBBY_MANAGEMENT  (1 << 2)
#define SOULLINK_CAP_RECONNECT_TOKEN   (1 << 3)
#define SOULLINK_CAP_STABLE_ENCOUNTERS (1 << 4)

// Mailbox status & overflow flags (mailbox.mailboxFlags)
#define SOULLINK_FLAG_QUEUE_OVERFLOW    (1 << 0) // Non-critical event dropped
#define SOULLINK_FLAG_CRITICAL_OVERFLOW (1 << 1) // Critical event forced into full queue
#define SOULLINK_FLAG_DIRTY_STATE       (1 << 2) // Unacknowledged state changes present
#define SOULLINK_FLAG_RECONNECT_NEEDED  (1 << 3) // Reconnection required

// Event flags (SoulLinkEvent.flags)
#define SOULLINK_EVT_FLAG_CRITICAL      (1 << 0) // Cannot be dropped (deaths, forfeits, wipes)
#define SOULLINK_EVT_FLAG_REPLAY        (1 << 1) // Replayed event for catch-up

// Command flags (SoulLinkCommand.flags)
#define SOULLINK_CMD_FLAG_CRITICAL      (1 << 0)
#define SOULLINK_CMD_FLAG_APPLIED       (1 << 1)

#define MAX_PLAYERS 8
#define NUM_MAPSECS 213 // Total trackable route/location IDs in Hoenn
#define PLAYER_NAME_LEN 8

#define SOULLINK_EVENT_QUEUE_SIZE 16
#define SOULLINK_CMD_QUEUE_SIZE   16
#define SOULLINK_QUEUE_MASK       15

#define SOULLINK_ROOM_CODE_LEN     8
#define SOULLINK_SESSION_TOKEN_LEN 8

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

// Transport connection state
enum SoulLinkTransportState {
    SOULLINK_TRANSPORT_OFFLINE = 0,
    SOULLINK_TRANSPORT_BRIDGE_DETECTED = 1,
    SOULLINK_TRANSPORT_CONNECTING = 2,
    SOULLINK_TRANSPORT_CONNECTED = 3,
    SOULLINK_TRANSPORT_DISCONNECTED = 4,
    SOULLINK_TRANSPORT_ERROR = 5,
};

// Lobby operation codes (ROM -> Bridge)
enum SoulLinkLobbyOp {
    SOULLINK_LOBBY_OP_NONE = 0,
    SOULLINK_LOBBY_OP_CREATE = 1,
    SOULLINK_LOBBY_OP_JOIN = 2,
    SOULLINK_LOBBY_OP_READY = 3,
    SOULLINK_LOBBY_OP_UNREADY = 4,
    SOULLINK_LOBBY_OP_START_RUN = 5,
    SOULLINK_LOBBY_OP_LEAVE = 6,
    SOULLINK_LOBBY_OP_RECONNECT = 7,
    SOULLINK_LOBBY_OP_GET_STATUS = 8,
};

// Lobby response codes (Bridge -> ROM)
enum SoulLinkResponseCode {
    SOULLINK_RESP_IDLE = 0,
    SOULLINK_RESP_OK = 1,
    SOULLINK_RESP_PENDING = 2,
    SOULLINK_RESP_ERR_NO_BRIDGE = 3,
    SOULLINK_RESP_ERR_NETWORK = 4,
    SOULLINK_RESP_ERR_ROOM_NOT_FOUND = 5,
    SOULLINK_RESP_ERR_ROOM_FULL = 6,
    SOULLINK_RESP_ERR_VERSION_MISMATCH = 7,
    SOULLINK_RESP_ERR_ALREADY_STARTED = 8,
    SOULLINK_RESP_ERR_NOT_AUTHORIZED = 9,
    SOULLINK_RESP_ERR_DISCONNECTED = 10,
};

// Lobby run states
enum SoulLinkRunState {
    SOULLINK_RUN_STATE_LOBBY = 0,
    SOULLINK_RUN_STATE_STARTING = 1,
    SOULLINK_RUN_STATE_IN_PROGRESS = 2,
};

// Route status in Soul Link matrix
enum SoulLinkRouteState {
    ROUTE_STATE_OPEN = 0,             // Not yet encountered
    ROUTE_STATE_ALIVE = 1,            // Caught and currently alive
    ROUTE_STATE_BOXED = 2,            // Boxed in PC storage
    ROUTE_STATE_DEAD_FORFEITED = 3,   // Fainted, fled, or encounter forfeited
    ROUTE_STATE_ENCOUNTER_ACTIVE = 4, // Currently in first encounter battle
};

// Reliable event structure (12 bytes)
struct SoulLinkEvent {
    u16 sequenceId;
    u8 eventType;
    u8 mapSec;
    u16 species;
    u8 primaryType;
    u8 slotIndex;
    u8 flags;
    u8 senderPlayerId;
    u16 timestamp;
};

// Reliable command structure (12 bytes)
struct SoulLinkCommand {
    u16 sequenceId;
    u8 cmdType;
    u8 targetMapSec;
    u16 paramSpecies;
    u8 paramPrimaryType;
    u8 sourcePlayerId;
    u8 flags;
    u8 executionStatus;
    u16 timestamp;
};

// In-game lobby request from ROM to Bridge (24 bytes)
struct SoulLinkLobbyRequest {
    u16 requestId;
    u8 opCode;
    u8 maxPlayers;
    u8 roomCode[SOULLINK_ROOM_CODE_LEN];
    u8 sessionToken[SOULLINK_SESSION_TOKEN_LEN];
    u8 padding[4];
};

// Lobby response from Bridge to ROM (36 bytes)
struct SoulLinkLobbyResponse {
    u16 ackRequestId;
    u8 opCode;
    u8 responseCode;
    u8 roomCode[SOULLINK_ROOM_CODE_LEN];
    u8 sessionToken[SOULLINK_SESSION_TOKEN_LEN];
    u8 assignedPlayerId;
    u8 activePlayerCount;
    u8 playerReadyMask;
    u8 runState;
    u8 message[12];
};

// Live player info in lobby / run (16 bytes)
struct PlayerSyncInfo {
    u8 isConnected;
    u8 isPaused;
    u8 badgeCount;
    u8 currentMapSec;
    u8 name[PLAYER_NAME_LEN];
    u8 isReady;
    u8 padding[3];
};

// Exact byte offsets into gSoulLink for Lua bridge auto-detection & tests
#define SOULLINK_OFFSET_MAGIC               0x0000
#define SOULLINK_OFFSET_PROTOCOL_VERSION    0x0004
#define SOULLINK_OFFSET_CAPABILITIES        0x0006
#define SOULLINK_OFFSET_TRANSPORT_STATE     0x0008
#define SOULLINK_OFFSET_MAILBOX_FLAGS       0x0009
#define SOULLINK_OFFSET_EVENT_HEAD          0x000C
#define SOULLINK_OFFSET_EVENT_TAIL          0x000E
#define SOULLINK_OFFSET_LAST_ACKED_EVENT    0x0010
#define SOULLINK_OFFSET_EVENT_DROP_COUNT    0x0012
#define SOULLINK_OFFSET_EVENT_QUEUE         0x0014
#define SOULLINK_OFFSET_CMD_HEAD            0x00D4
#define SOULLINK_OFFSET_CMD_TAIL            0x00D6
#define SOULLINK_OFFSET_LAST_EXEC_CMD       0x00D8
#define SOULLINK_OFFSET_CMD_DROP_COUNT      0x00DA
#define SOULLINK_OFFSET_CMD_QUEUE           0x00DC
#define SOULLINK_OFFSET_LOBBY_REQUEST       0x019C
#define SOULLINK_OFFSET_LOBBY_RESPONSE      0x01B4
#define SOULLINK_OFFSET_LEGACY_OUT_TYPE     0x01D8
#define SOULLINK_OFFSET_PLAYER_COUNT        0x01E4
#define SOULLINK_OFFSET_UNLOCKED_GYM        0x01E5
#define SOULLINK_OFFSET_ROUTE_STATE         0x01E6
#define SOULLINK_OFFSET_ROUTE_SPECIES       0x02BC
#define SOULLINK_OFFSET_ROUTE_TYPES_MASK    0x100C
#define SOULLINK_OFFSET_PLAYERS             0x1360
#define SOULLINK_MAILBOX_TOTAL_SIZE         0x13E0

struct SoulLinkMailbox {
    // 0x0000: Magic header at offset 0 for mGBA Lua bridge auto-detection
    u32 magic;

    // 0x0004: Version and capabilities
    u16 protocolVersion;
    u16 capabilities;
    u8 transportState;
    u8 mailboxFlags;
    u16 reservedHeader;

    // 0x000C: Outgoing Event Ring Buffer (ROM -> Bridge)
    u16 eventHead;
    u16 eventTail;
    u16 lastAckedEventSequenceId;
    u16 eventDropCount;
    struct SoulLinkEvent eventQueue[SOULLINK_EVENT_QUEUE_SIZE]; // 16 * 12 = 192 bytes (0x0014 - 0x00D4)

    // 0x00D4: Incoming Command Ring Buffer (Bridge -> ROM)
    u16 cmdHead;
    u16 cmdTail;
    u16 lastExecutedCmdSequenceId;
    u16 cmdDropCount;
    struct SoulLinkCommand cmdQueue[SOULLINK_CMD_QUEUE_SIZE]; // 16 * 12 = 192 bytes (0x00DC - 0x019C)

    // 0x019C: In-Game Lobby Handshake
    struct SoulLinkLobbyRequest lobbyRequest;   // 24 bytes (0x019C - 0x01B4)
    struct SoulLinkLobbyResponse lobbyResponse; // 36 bytes (0x01B4 - 0x01D8)

    // 0x01D8: Legacy single-slot mirrors (for backward compatibility)
    u8 legacyOutEventType;
    u8 legacyOutMapSec;
    u16 legacyOutSpecies;
    u8 legacyOutPrimaryType;
    u8 legacyOutSlotIndex;
    u8 legacyInCommandType;
    u8 legacyInTargetMapSec;
    u8 legacyInNotificationPlayerId;
    u8 legacyPadding[3];

    // 0x01E4: Persistent Synced State
    u8 playerCount;                             // 1 to 8
    u8 unlockedGymStage;                        // Highest gym currently allowed (0 to 8)
    u8 routeState[NUM_MAPSECS];                 // 0 = Open, 1 = Alive, 2 = Boxed, 3 = Dead/Forfeited, 4 = Active
    u8 paddingRoute;                            // Alignment to 2-byte boundary
    u16 routeSpecies[NUM_MAPSECS][MAX_PLAYERS]; // Species caught per route per player for Tracker UI
    u32 routeTypesMask[NUM_MAPSECS];            // 32-bit bitmask of primary types used within each route link
    struct PlayerSyncInfo players[MAX_PLAYERS]; // Live status, badges, and current route of all 8 players (8 * 16 B)
};

// Aliases for backward compatibility
#define outEventType legacyOutEventType
#define outMapSec legacyOutMapSec
#define outSpecies legacyOutSpecies
#define outPrimaryType legacyOutPrimaryType
#define outSlotIndex legacyOutSlotIndex
#define inCommandType legacyInCommandType
#define inTargetMapSec legacyInTargetMapSec
#define inNotificationPlayerId legacyInNotificationPlayerId

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

// Queue and Event Management (Phase 3 Step 1)
bool8 SoulLink_PushEvent(u8 eventType, u8 mapSec, u16 species, u8 primaryType, u8 slotIndex, u8 flags);
bool8 SoulLink_PopEvent(struct SoulLinkEvent *outEvent);
void SoulLink_AcknowledgeEvents(u16 sequenceId);
u16 SoulLink_GetPendingEventCount(void);
u16 SoulLink_GetEventDropCount(void);

bool8 SoulLink_ReceiveCommand(const struct SoulLinkCommand *cmd);
u16 SoulLink_GetPendingCommandCount(void);

// Lobby Request Operations (Phase 3 Step 1)
void SoulLink_LobbyRequestCreate(u8 maxPlayers);
void SoulLink_LobbyRequestJoin(const u8 *roomCode);
void SoulLink_LobbyRequestReady(bool8 ready);
void SoulLink_LobbyRequestStartRun(void);
void SoulLink_LobbyRequestLeave(void);
void SoulLink_LobbyRequestReconnect(const u8 *roomCode, const u8 *sessionToken);
void SoulLink_LobbyRequestGetStatus(void);

// Transport State Management
void SoulLink_SetTransportState(u8 state);
u8 SoulLink_GetTransportState(void);

// Phase 2: Local Nuzlocke Mechanics
void SoulLink_OnWildBattleStart(void);
void SoulLink_OnWildCatchSuccess(u16 species);
void SoulLink_OnWildEncounterFailed(void);
void SoulLink_RecordFaintEvent(u8 partyIndex);
void SoulLink_ClearFaintEvents(void);
void SoulLink_ProcessFaintedMonsPostBattle(void);
void SoulLink_HandleFollowerBattlePartyEnd(void);
bool8 SoulLink_IsPartyWiped(void);
u8 SoulLink_GetLivingPokemonCount(void);
u32 SoulLink_GetLivingBoxPokemonCount(void);
u32 SoulLink_GetTotalUsablePokemonCount(void);

// Gym progression
bool8 CheckPartyGymUnlock(void);
bool8 SoulLink_IsGymStageUnlocked(u8 stage);
u8 SoulLink_GetRequiredGymStageForMap(void);
u8 SoulLink_GetBadgeCount(void);
void SoulLink_OnBadgeEarned(void);

#if TESTING
void SoulLink_TestSetNextEventSequenceId(u16 seq);
#endif

#endif // GUARD_SOULLINK_H
