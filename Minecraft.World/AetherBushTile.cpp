#include "stdafx.h"
#include "AetherBushTile.h"
#include "net.minecraft.world.level.h"

AetherBushTile::AetherBushTile(int id) : Bush(id)
{
}

AetherBushTile::AetherBushTile(int id, Material *material) : Bush(id, material)
{
}

bool AetherBushTile::mayPlaceOn(int tile)
{
	return tile == Tile::aetherGrass_Id || tile == Tile::aetherDirt_Id;
}
