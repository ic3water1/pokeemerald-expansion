#ifndef GUARD_SOULLINK_RUN_H
#define GUARD_SOULLINK_RUN_H
#include "global.h"
#include "soullink_run_types.h"
enum SoulLinkRunStatus
{
    SOULLINK_RUN_SOLO,
    SOULLINK_RUN_RECONCILE,
    SOULLINK_RUN_AUTHORIZED,
    SOULLINK_RUN_FOREIGN,
    SOULLINK_RUN_CORRUPT,
};
extern const volatile struct SoulLinkRunProfile gSoulLinkRunProfile;
u32 SoulLinkRun_Crc32(const void *data, u32 size);
bool8 SoulLinkRun_Validate(const struct SoulLinkRunIdentity *identity);
void SoulLinkRun_OnLoad(u8 saveStatus);
void SoulLinkRun_OnNewGame(void);
bool8 SoulLinkRun_PrepareSave(void);
bool8 SoulLinkRun_CanProgress(void);
u8 SoulLinkRun_GetStatus(void);
// Trusted adapter integration must lock on every disconnect/reset/save-state load BEFORE resuming the core.
void SoulLinkRun_Lock(void);
bool8 SoulLinkRun_ConfirmApplied(const struct SoulLinkRunIdentity *identity, u32 revision);
#endif
