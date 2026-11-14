#ifndef GUARD_SOULLINK_FEEDBACK_H
#define GUARD_SOULLINK_FEEDBACK_H
#include "global.h"
enum SoulLinkCatchStatus { SL_CATCH_NONE, SL_CATCH_AVAILABLE, SL_CATCH_USED, SL_CATCH_FORFEITED, SL_CATCH_LOCKED, SL_CATCH_SYNCING };
enum SoulLinkNoticeKind { SL_NOTICE_CATCH=1, SL_NOTICE_MISS, SL_NOTICE_DEATH, SL_NOTICE_BADGE, SL_NOTICE_LOST, SL_NOTICE_RECONNECT, SL_NOTICE_SYNCED };
void SoulLinkFeedback_Init(void);
u8 SoulLinkFeedback_GetCatchStatus(void);
const u8 *SoulLinkFeedback_StatusText(u8 status);
void SoulLinkFeedback_EnterArea(u16 section);
bool8 SoulLinkFeedback_BannerEntry(void);
void SoulLinkFeedback_BannerShown(void);
bool8 SoulLinkFeedback_CanPresent(void);
void SoulLinkFeedback_Enqueue(u8 kind, u8 player, u8 route, u16 species, u16 progress);
void SoulLinkFeedback_Update(void);
void SoulLinkFeedback_Hide(void);
void SoulLinkFeedback_WindowsReset(void);
u8 SoulLinkFeedback_Pending(void);
u16 SoulLinkFeedback_Dropped(void);
#if TESTING
u8 SoulLinkFeedback_TestKind(u8 index);
#endif
#endif
