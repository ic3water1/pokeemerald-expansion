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

/**
 * Protocol version (Phase 3: Versioned Mailbox Contract)
 *
 * BREAKING CHANGE NOTICE:
 * Protocol Version 1 redesigns the mailbox layout starting at offset 0x0004 with protocolVersion,
 * replacing the legacy Phase 1/Phase 2 single-slot memory polling mechanism with reliable ring buffers.
 * Legacy memory readers that directly poll offset 0x0004 for single-slot events are binary-incompatible
 * and must be upgraded to support Protocol Version 1.
 */
#define SOULLINK_PROTOCOL_VERSION 1

// Capability flags (bitmask in mailbox.capabilities)
#define SOULLINK_CAP_EVENT_QUEUE       (1 << 0)
#define SOULLINK_CAP_CMD_QUEUE         (1 << 1)
#define SOULLINK_CAP_LOBBY_MANAGEMENT  (1 << 2)
#define SOULLINK_CAP_RECONNECT_TOKEN   (1 << 3)
#define SOULLINK_CAP_STABLE_ENCOUNTERS (1 << 4)
#define SOULLINK_CAP_RUN_FENCE_V2      (1 << 5)

// Mailbox status & overflow flags (mailbox.mailboxFlags)
#define SOULLINK_FLAG_QUEUE_OVERFLOW           (1 << 0) // Non-critical event dropped
#define SOULLINK_FLAG_CRITICAL_OVERFLOW        (1 << 1) // Critical event overflow occurred
#define SOULLINK_FLAG_DIRTY_STATE              (1 << 2) // Unacknowledged state changes present
#define SOULLINK_FLAG_RECONNECT_NEEDED         (1 << 3) // Reconnection required
#define SOULLINK_FLAG_RECONCILIATION_REQUIRED  (1 << 4) // Full state reconciliation needed on reconnect
#if SOULLINK_DEV_TEST || TESTING
#define SOULLINK_FLAG_DEV_SAFE_OVERWORLD       (1 << 6) // Automated test: enables safe overworld simulation (dev build only)
#endif

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
    SOULLINK_CMD_CONFIRM_PLAN = 5,
    SOULLINK_CMD_AUTHORIZE_PLAN = 6,
    SOULLINK_CMD_EVENT_FLOOR = 7,
};

// Incoming Command Execution Status
enum SoulLinkCmdExecStatus {
    SOULLINK_CMD_STATUS_PENDING = 0,
    SOULLINK_CMD_STATUS_SUCCESS = 1,
    SOULLINK_CMD_STATUS_REJECTED = 2,
    SOULLINK_CMD_STATUS_DUPLICATE = 3,
    SOULLINK_CMD_STATUS_UNIMPLEMENTED = 4,
    SOULLINK_CMD_STATUS_DEFERRED = 5,
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
    SOULLINK_RESP_ERR_BUSY = 11,
    SOULLINK_RESP_ERR_INVALID_PARAM = 12,
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
    u16 sequenceId;     // Monotonic sequence (1..65535, skipping 0)
    u8 eventType;       // enum SoulLinkEventType
    u8 mapSec;          // Region map section ID
    u16 species;        // Caught species
    u8 primaryType;     // Primary type
    u8 slotIndex;       // Party slot index
    u8 flags;           // SOULLINK_EVT_FLAG_*
    u8 senderPlayerId;  // Local player ID (0)
    u16 timestamp;      // Timestamp / tick counter
};

// Reliable command structure (12 bytes)
struct SoulLinkCommand {
    u16 sequenceId;      // Sequence number from coordinator
    u8 cmdType;          // enum SoulLinkCommandType
    u8 targetMapSec;     // Target route ID
    u16 paramValue;      // Target gym stage (for UNLOCK_GYM), species, or secondary param
    u8 paramPrimaryType; // Primary type
    u8 sourcePlayerId;   // Player ID who triggered event
    u8 flags;            // SOULLINK_CMD_FLAG_*
    u8 executionStatus;  // enum SoulLinkCmdExecStatus
    u16 timestamp;       // Timestamp
};

// In-game lobby request from ROM to Bridge (24 bytes)
struct SoulLinkLobbyRequest {
    u16 requestId;                          // Monotonic request ID
    u8 opCode;                              // enum SoulLinkLobbyOp
    u8 maxPlayers;                          // 2..8
    u8 roomCode[SOULLINK_ROOM_CODE_LEN];    // Bounded room code (null-terminated)
    u8 sessionToken[SOULLINK_SESSION_TOKEN_LEN]; // Opaque session token
    u8 padding[4];
};

// Lobby response from Bridge to ROM (36 bytes)
struct SoulLinkLobbyResponse {
    u16 ackRequestId;                       // Matches request.requestId
    u8 opCode;                              // Matches request.opCode
    u8 responseCode;                        // enum SoulLinkResponseCode
    u8 roomCode[SOULLINK_ROOM_CODE_LEN];    // Room code returned
    u8 sessionToken[SOULLINK_SESSION_TOKEN_LEN]; // Rejoin session token
    u8 assignedPlayerId;                    // 0..7
    u8 activePlayerCount;                   // 1..8
    u8 playerReadyMask;                     // bitmask of ready players
    u8 runState;                            // enum SoulLinkRunState
    u8 hostPlayerId;                        // Authoritative host player ID
    u8 occupiedSlotsMask;                   // Bitmask of currently occupied player slots
    u8 message[10];                         // Status/error text
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
#define SOULLINK_OFFSET_SESSION_EPOCH       0x0008
#define SOULLINK_OFFSET_TRANSPORT_STATE     0x000C
#define SOULLINK_OFFSET_MAILBOX_FLAGS       0x000D
#define SOULLINK_OFFSET_EVENT_DROP_COUNT    0x000E
#define SOULLINK_OFFSET_CRITICAL_OVERFLOW   0x0010
#define SOULLINK_OFFSET_LAST_ACKED_EVENT    0x0012
#define SOULLINK_OFFSET_EVENT_HEAD          0x0014
#define SOULLINK_OFFSET_EVENT_TAIL          0x0016
#define SOULLINK_OFFSET_EVENT_QUEUE         0x0018
#define SOULLINK_OFFSET_CMD_HEAD            0x00D8
#define SOULLINK_OFFSET_CMD_TAIL            0x00DA
#define SOULLINK_OFFSET_LAST_EXEC_CMD       0x00DC
#define SOULLINK_OFFSET_CMD_DROP_COUNT      0x00DE
#define SOULLINK_OFFSET_CMD_QUEUE           0x00E0
#define SOULLINK_OFFSET_LOBBY_REQUEST       0x01A0
#define SOULLINK_OFFSET_LOBBY_RESPONSE      0x01B8
#define SOULLINK_OFFSET_PLAYER_COUNT        0x01DC
#define SOULLINK_OFFSET_UNLOCKED_GYM        0x01DD
#define SOULLINK_OFFSET_ROUTE_STATE         0x01DE
#define SOULLINK_OFFSET_RUN_STATUS          0x02B3
#define SOULLINK_OFFSET_ROUTE_SPECIES       0x02B4
#define SOULLINK_OFFSET_ROUTE_TYPES_MASK    0x1004
#define SOULLINK_OFFSET_PLAYERS             0x1358
#define SOULLINK_MAILBOX_TOTAL_SIZE         0x13D8

struct SoulLinkMailbox {
    // 0x0000: Magic header ("SOUL" in ASCII little-endian: 0x4C554F53)
    u32 magic;

    // 0x0004: Version, capabilities, and session identity
    u16 protocolVersion;                        // 0x0004 (2 bytes) = 1
    u16 capabilities;                           // 0x0006 (2 bytes)
    u32 sessionEpoch;                           // 0x0008 (4 bytes) = unique session ID per ROM boot/reset
    u8 transportState;                          // 0x000C (1 byte)
    u8 mailboxFlags;                            // 0x000D (1 byte)
    u16 eventDropCount;                         // 0x000E (2 bytes) - non-critical dropped count
    u16 criticalOverflowCount;                  // 0x0010 (2 bytes) - critical event overflow count
    u16 lastAckedEventSequenceId;               // 0x0012 (2 bytes) - acknowledged sequence (Bridge writes)

    // 0x0014: Outgoing Event Ring Buffer Management (ROM -> Bridge)
    u16 eventHead;                              // 0x0014 (2 bytes) - write index (ROM owns)
    u16 eventTail;                              // 0x0016 (2 bytes) - read index (Bridge owns)
    struct SoulLinkEvent eventQueue[SOULLINK_EVENT_QUEUE_SIZE]; // 0x0018 (16 * 12 = 192 bytes = 0xC0)

    // 0x00D8: Incoming Command Ring Buffer Management (Bridge -> ROM)
    u16 cmdHead;                                // 0x00D8 (2 bytes) - write index (Bridge owns)
    u16 cmdTail;                                // 0x00DA (2 bytes) - read index (ROM owns)
    u16 lastExecutedCmdSequenceId;              // 0x00DC (2 bytes) - sequence executed by ROM
    u16 cmdDropCount;                           // 0x00DE (2 bytes) - dropped incoming command count
    struct SoulLinkCommand cmdQueue[SOULLINK_CMD_QUEUE_SIZE]; // 0x00E0 (16 * 12 = 192 bytes = 0xC0)

    // 0x01A0: In-Game Lobby Handshake
    struct SoulLinkLobbyRequest lobbyRequest;   // 0x01A0 (24 bytes = 0x18)
    struct SoulLinkLobbyResponse lobbyResponse; // 0x01B8 (36 bytes = 0x24)

    // 0x01DC: Persistent Synced State
    u8 playerCount;                             // 0x01DC (1 byte)
    u8 unlockedGymStage;                        // 0x01DD (1 byte)
    u8 routeState[NUM_MAPSECS];                 // 0x01DE (213 bytes)
    u8 runStatus;                               // 0x02B3 (1 byte, enum SoulLinkRunStatus)
    u16 routeSpecies[NUM_MAPSECS][MAX_PLAYERS]; // 0x02B4 (213 * 8 * 2 = 3408 bytes = 0x0D50)
    u32 routeTypesMask[NUM_MAPSECS];            // 0x1004 (213 * 4 = 852 bytes = 0x0354)
    struct PlayerSyncInfo players[MAX_PLAYERS]; // 0x1358 (8 * 16 = 128 bytes = 0x80)
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
bool8 SoulLink_IsSafeOverworldState(void);
void SoulLink_ProcessOverworldCommands(void);

// Queue and Event Management (Phase 3 Step 1)
bool8 SoulLink_PushEvent(u8 eventType, u8 mapSec, u16 species, u8 primaryType, u8 slotIndex, u8 flags);
bool8 SoulLink_PopEvent(struct SoulLinkEvent *outEvent);
void SoulLink_AcknowledgeEvents(u16 sequenceId);
u16 SoulLink_GetPendingEventCount(void);
u16 SoulLink_GetEventDropCount(void);
u16 SoulLink_GetCriticalOverflowCount(void);

bool8 SoulLink_ReceiveCommand(const struct SoulLinkCommand *cmd);
u16 SoulLink_GetPendingCommandCount(void);

// Lobby Request Operations (Phase 3 Step 1)
bool8 SoulLink_IsLobbyRequestPending(void);
void SoulLink_CancelLobbyRequest(void);
bool8 SoulLink_LobbyRequestCreate(u8 maxPlayers);
bool8 SoulLink_LobbyRequestJoin(const u8 *roomCode);
bool8 SoulLink_LobbyRequestReady(bool8 ready);
bool8 SoulLink_LobbyRequestStartRun(void);
bool8 SoulLink_LobbyRequestLeave(void);
bool8 SoulLink_LobbyRequestReconnect(const u8 *roomCode, const u8 *sessionToken);
bool8 SoulLink_LobbyRequestGetStatus(void);

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
void SoulLink_SweepRetainedDeadMons(void);

// Gym progression
bool8 CheckPartyGymUnlock(void);
bool8 SoulLink_IsGymStageUnlocked(u8 stage);
u8 SoulLink_GetRequiredGymStageForMap(void);
u8 SoulLink_GetBadgeCount(void);
void SoulLink_OnBadgeEarned(void);
bool8 SoulLink_IsMultiplayerActive(void);
u8 SoulLink_ApplyKillMon(u8 targetMapSec, u16 species);

#if TESTING
void SoulLink_TestSetNextEventSequenceId(u16 seq);
void SoulLink_TestSetSessionEpoch(u32 epoch);
void SoulLink_TestSetSafeOverworldOverride(bool8 enable, bool8 isSafe);
void SoulLink_TestSetLastExecutedCmdSequenceId(u16 seq);
#endif

#endif // GUARD_SOULLINK_H
