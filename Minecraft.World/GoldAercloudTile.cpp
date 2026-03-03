#include "stdafx.h"
#include "GoldAercloudTile.h"

GoldAercloudTile::GoldAercloudTile(int id) : AercloudTile(id)
{
}

int GoldAercloudTile::getColor() const
{
	return 0xFFEF91; // Gold
}

int GoldAercloudTile::getColor(int auxData)
{
	return 0xFFEF91;
}

int GoldAercloudTile::getColor(LevelSource *level, int x, int y, int z)
{
	return 0xFFEF91;
}

int GoldAercloudTile::getColor(LevelSource *level, int x, int y, int z, int data)
{
	return 0xFFEF91;
}
