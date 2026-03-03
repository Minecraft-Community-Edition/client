#include "stdafx.h"
#include "AetherSaplingTile.h"
#include "SkyrootTreeFeature.h"
#include "GoldenOakTreeFeature.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"
#include "net.minecraft.world.h"

AetherSaplingTile::AetherSaplingTile(int id) : AetherBushTile(id)
{
}

void AetherSaplingTile::tick(Level *level, int x, int y, int z, Random *random)
{
	if (level->isClientSide) return;

	Bush::tick(level, x, y, z, random);

	if (level->getRawBrightness(x, y + 1, z) >= Level::MAX_BRIGHTNESS - 6)
	{
		if (random->nextInt(7) == 0)
		{
			int data = level->getData(x, y, z);
			if ((data & AGE_BIT) == 0)
			{
				level->setData(x, y, z, data | AGE_BIT);
			}
			else
			{
				growTree(level, x, y, z, random);
			}
		}
	}
}

void AetherSaplingTile::growTree(Level *level, int x, int y, int z, Random *random)
{
	Feature *f = NULL;

	if (this->id == Tile::skyrootSapling_Id)
	{
		f = new SkyrootTreeFeature(true);
	}
	else if (this->id == Tile::goldenOakSapling_Id)
	{
		f = new GoldenOakTreeFeature(true);
	}
	else
	{
		return;
	}

	// Remove sapling before attempting to place tree
	level->setTileNoUpdate(x, y, z, 0);

	if (!f->place(level, random, x, y, z))
	{
		// Restore sapling if tree placement failed
		int data = level->getData(x, y, z);
		level->setTileAndDataNoUpdate(x, y, z, this->id, data);
	}

	delete f;
}
