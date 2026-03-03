#include "stdafx.h"
#include "AetherLeafTile.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.h"

AetherLeafTile::AetherLeafTile(int id) : TransparentTile(id, Material::leaves, true, isSolidRender())
{
	setTicking(false);
}

int AetherLeafTile::getResource(int data, Random *random, int playerBonusLevel)
{
	// Skyroot leaves drop skyroot saplings, golden oak leaves drop golden oak saplings
	if (id == Tile::goldenOakLeaves_Id) return Tile::goldenOakSapling_Id;
	return Tile::skyrootSapling_Id;
}

int AetherLeafTile::getResourceCount(Random *random)
{
	return random->nextInt(20) == 0 ? 1 : 0;
}

bool AetherLeafTile::isSeasonalLeaf() const
{
	return false;
}
