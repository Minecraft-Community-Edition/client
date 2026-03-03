#include "stdafx.h"
#include "GoldenOakLogTile.h"
#include "AetherNaturalTile.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.entity.player.h"
#include "net.minecraft.world.level.h"
#include "Mth.h"

GoldenOakLogTile::GoldenOakLogTile(int id, const wstring &sideTexture, const wstring &topTexture)
	: SkyrootLogTile(id, sideTexture, topTexture)
{
}

int GoldenOakLogTile::getResource(int data, Random *random, int playerBonusLevel)
{
	// Golden oak logs drop skyroot logs instead of themselves
	return Tile::skyrootLog_Id;
}

void GoldenOakLogTile::playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data)
{
	// Handle skyroot log drops (1, or 2 with skyroot hatchet)
	SkyrootLogTile::playerDestroy(level, player, x, y, z, data);

	// Additionally drop 1-2 golden amber
	if (!level->isClientSide)
	{
		int count = 1 + Mth::nextInt(level->random, 0, 1);
		for (int i = 0; i < count; i++)
		{
			popResource(level, x, y, z, shared_ptr<ItemInstance>(new ItemInstance(Item::goldenAmber_Id, 1, 0)));
		}
	}
}
