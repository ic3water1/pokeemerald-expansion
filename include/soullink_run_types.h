#ifndef GUARD_SOULLINK_RUN_TYPES_H
#define GUARD_SOULLINK_RUN_TYPES_H
// Kept separate from the Step 4 mailbox. Serialized little-endian; IDs are opaque bytes.
#define SOULLINK_RUN_MAGIC 0x31524C53 // SLR1
#define SOULLINK_RUN_SAVE_VERSION 1
#define SOULLINK_RUN_PROTOCOL_VERSION 2
struct SoulLinkRunIdentity
{
    u32 magic;
    u16 saveVersion;
    u16 protocolVersion;
    u8 runId[16];
    u8 participantId[16];
    u8 playerId;
    u8 reserved[3];
};
struct SoulLinkRunSave
{
    struct SoulLinkRunIdentity identity;
    u32 authoritativeRevision;
    u32 nextEventId;
    u32 checksum; // CRC32 of SaveBlock3 through the byte before this checksum
};
struct SoulLinkRunProfile
{
    u8 marker[16];
    struct SoulLinkRunIdentity identity;
    u32 checksum; // CRC32 of identity only; all-zero identity means ordinary solo ROM
};
#endif
