#ifndef GUARD_SOULLINK_MENU_H
#define GUARD_SOULLINK_MENU_H

#include "main.h"

void CB2_InitSoulLinkMenu(void);

u16 SoulLink_GetFaceCodeSpecies(u8 index);
const u8 *SoulLink_GetFaceCodeName(u8 index);
s8 SoulLink_HexCharToFaceIndex(u8 c);
bool8 SoulLink_FormatFaceCodeString(u8 *dest, const u8 *indices, u8 count);
bool8 SoulLink_ParseFaceCodeString(const u8 *code, u8 *outIndices, u8 count);

#endif // GUARD_SOULLINK_MENU_H
