// Layout conformance only. No initializer or debug controls enter release ROMs.
#include "global.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "fieldmap.h"
#include "battle.h"
#include "test/test.h"
TEST("AcceptanceFixture: external fixture layout matches native serialization")
{
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
