#include "global.h"
#include "save.h"
#include "load_save.h"
#include "soullink.h"
#include "soullink_run.h"
#include "test_runner.h"

// The launcher patches a COPY of the ROM, never the original solo ROM or save.
const volatile struct SoulLinkRunProfile gSoulLinkRunProfile = {
    .marker = "SL_PROFILE_V1",
};
static EWRAM_DATA u8 sRunStatus = SOULLINK_RUN_SOLO;
STATIC_ASSERT(sizeof(struct SoulLinkRunIdentity) == 44, SoulLinkRunIdentityWireSize);
STATIC_ASSERT(sizeof(struct SoulLinkRunSave) == 56, SoulLinkRunSaveWireSize);
STATIC_ASSERT(offsetof(struct SaveBlock3, runIntegrity) == 1496, SoulLinkRunAppendOnlyLayout);

u32 SoulLinkRun_Crc32(const void *data, u32 size)
{
    const u8 *bytes = data;
    u32 crc = 0xFFFFFFFF;
    u32 i, bit;
    for (i = 0; i < size; i++)
    {
        crc ^= bytes[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return ~crc;
}

static bool8 IsZero(const void *data, u32 size)
{
    const u8 *bytes = data;
    u32 i;
    for (i = 0; i < size; i++)
        if (bytes[i] != 0)
            return FALSE;
    return TRUE;
}

bool8 SoulLinkRun_Validate(const struct SoulLinkRunIdentity *id)
{
    return id != NULL && id->magic == SOULLINK_RUN_MAGIC
        && id->saveVersion == SOULLINK_RUN_SAVE_VERSION
        && id->protocolVersion == SOULLINK_RUN_PROTOCOL_VERSION
        && id->playerId < 8 && IsZero(id->reserved, sizeof(id->reserved))
        && !IsZero(id->runId, sizeof(id->runId))
        && !IsZero(id->participantId, sizeof(id->participantId));
}

static u32 SaveChecksum(void)
{
    return SoulLinkRun_Crc32(gSaveBlock3Ptr, offsetof(struct SaveBlock3, runIntegrity.checksum));
}

void SoulLinkRun_SetStatus(u8 status)
{
    sRunStatus = status;
    gSoulLink.runStatus = status;
}

void SoulLinkRun_OnLoad(u8 saveStatus)
{
    struct SoulLinkRunIdentity profile;
    memcpy(&profile, (const void *)&gSoulLinkRunProfile.identity, sizeof(profile));
    if (IsZero(&profile, sizeof(profile)))
    {
        // Legacy solo data remains in place. A multiplayer save is NEVER accepted by a solo ROM.
        SoulLinkRun_SetStatus(IsZero(&gSaveBlock3Ptr->runIntegrity, sizeof(gSaveBlock3Ptr->runIntegrity)) ? SOULLINK_RUN_SOLO : SOULLINK_RUN_FOREIGN);
        return;
    }
    SoulLinkRun_SetStatus(SOULLINK_RUN_CORRUPT);
    if (!SoulLinkRun_Validate(&profile) || gSoulLinkRunProfile.checksum != SoulLinkRun_Crc32(&profile, sizeof(profile)))
        return;
    if (saveStatus == SAVE_STATUS_EMPTY)
    {
        SoulLinkRun_SetStatus(SOULLINK_RUN_RECONCILE); // Only an empty profile can become a NEW multiplayer game.
        return;
    }
    if (saveStatus != SAVE_STATUS_OK || gSaveBlock3Ptr->runIntegrity.checksum != SaveChecksum())
        return;
    if (memcmp(&profile, &gSaveBlock3Ptr->runIntegrity.identity, sizeof(profile)) != 0)
    {
        SoulLinkRun_SetStatus(SOULLINK_RUN_FOREIGN);
        return;
    }
    // SRAM contains identity and a checkpoint, never an authorization grant.
    SoulLinkRun_SetStatus(SOULLINK_RUN_RECONCILE);
}

void SoulLinkRun_OnNewGame(void)
{
    struct SoulLinkRunIdentity profile;
    memcpy(&profile, (const void *)&gSoulLinkRunProfile.identity, sizeof(profile));
    if (IsZero(&profile, sizeof(profile)))
        return; // Solo format and progress are not converted.
    if (sRunStatus != SOULLINK_RUN_RECONCILE || gSaveFileStatus != SAVE_STATUS_EMPTY)
    {
        SoulLinkRun_SetStatus(SOULLINK_RUN_FOREIGN);
        return;
    }
    gSaveBlock3Ptr->runIntegrity.identity = profile;
    gSaveBlock3Ptr->runIntegrity.nextEventId = 1;
    gSaveBlock3Ptr->runIntegrity.authoritativeRevision = 0;
    gSaveBlock3Ptr->runIntegrity.checksum = SaveChecksum();
}

bool8 SoulLinkRun_PrepareSave(void)
{
    if (sRunStatus == SOULLINK_RUN_SOLO)
        return TRUE;
    if (sRunStatus != SOULLINK_RUN_AUTHORIZED)
        return FALSE;
    gSaveBlock3Ptr->runIntegrity.checksum = SaveChecksum();
    return TRUE;
}

bool8 SoulLinkRun_CanProgress(void)
{
    return sRunStatus == SOULLINK_RUN_SOLO || sRunStatus == SOULLINK_RUN_AUTHORIZED;
}

u8 SoulLinkRun_GetStatus(void) { return sRunStatus; }
void SoulLinkRun_Lock(void)
{
    if (sRunStatus == SOULLINK_RUN_AUTHORIZED)
        SoulLinkRun_SetStatus(SOULLINK_RUN_RECONCILE);
}

bool8 SoulLinkRun_ConfirmApplied(const struct SoulLinkRunIdentity *identity, u32 revision)
{
    if (sRunStatus != SOULLINK_RUN_RECONCILE || !SoulLinkRun_Validate(identity)
     || memcmp(identity, &gSaveBlock3Ptr->runIntegrity.identity, sizeof(*identity)) != 0
     || revision < gSaveBlock3Ptr->runIntegrity.authoritativeRevision)
        return FALSE;
    // Call only after the bridge authenticates, applies authoritative plan and waits for native enforcement.
    gSaveBlock3Ptr->runIntegrity.authoritativeRevision = revision;
    SoulLinkRun_SetStatus(SOULLINK_RUN_AUTHORIZED);
    return TRUE;
}
