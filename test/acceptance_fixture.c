// Layout conformance only. No initializer or debug controls enter release ROMs.
#include "global.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "fieldmap.h"
#include "battle.h"
#include "window.h"
#include "constants/flags.h"
#include "constants/vars.h"
#include "test/test.h"
TEST("AcceptanceFixture: external fixture layout matches native serialization")
{
    EXPECT_EQ(offsetof(struct SaveBlock1, vars), 0x139C);
    EXPECT_EQ(VAR_ROUTE101_STATE, 0x4060);
    EXPECT_EQ(FLAG_RESCUED_BIRCH, 0x52);
    EXPECT_EQ(FLAG_HIDE_ROUTE_101_BIRCH_STARTERS_BAG, 0x2BC);
    EXPECT_EQ(FLAG_HIDE_ROUTE_101_BIRCH_ZIGZAGOON_BATTLE, 0x2D0);
    EXPECT_EQ(FLAG_SYS_POKEMON_GET, 0x860);
    EXPECT_EQ(sizeof(struct Window), 12);
    EXPECT_EQ(offsetof(struct Window, tileData), 8);
    EXPECT_EQ(offsetof(struct BattleResults, caughtMonSpecies), 0x2C);
    EXPECT_EQ(sizeof(struct Pokemon), 100);
    EXPECT_EQ(sizeof(struct BoxPokemon), 80);
    EXPECT_EQ(offsetof(struct Pokemon, hp), 86);
    EXPECT_EQ(offsetof(struct PokemonStorage, boxes), 4);
    EXPECT_EQ(offsetof(struct SaveBlock1, playerParty), 0x238);
    EXPECT_EQ(offsetof(struct SaveBlock1, flags), 0x1270);
    EXPECT_EQ(offsetof(struct MapHeader, regionMapSectionId), 0x14);
    EXPECT_EQ(offsetof(struct SaveBlock1, lastHealLocation), 0x1C);
}
