#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"
#include "GoldenOakTreeFeature.h"

GoldenOakTreeFeature::GoldenOakTreeFeature(bool doUpdate) : Feature(doUpdate)
{
}

bool GoldenOakTreeFeature::place(Level *level, Random *random, int x, int y, int z)
{
	// Spruce-style conical tree with golden oak logs and leaves
	int treeHeight = random->nextInt(4) + 6;
	int trunkHeight = 1 + random->nextInt(2);
	int topHeight = treeHeight - trunkHeight;
	int leafRadius = 2 + random->nextInt(2);

	bool free = true;
	if (y < 1 || y + treeHeight + 1 > Level::maxBuildHeight) return false;

	// 4J Stu Added to stop tree features generating areas previously placed by game rule generation
	if (app.getLevelGenerationOptions() != NULL)
	{
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		bool intersects = levelGenOptions->checkIntersects(x - leafRadius, y - 1, z - leafRadius, x + leafRadius, y + treeHeight, z + leafRadius);
		if (intersects)
		{
			return false;
		}
	}

	// Make sure there is enough space
	for (int yy = y; yy <= y + 1 + treeHeight && free; yy++)
	{
		int r = 1;
		if ((yy - y) < trunkHeight)
		{
			r = 0;
		}
		else
		{
			r = leafRadius;
		}
		for (int xx = x - r; xx <= x + r && free; xx++)
		{
			for (int zz = z - r; zz <= z + r && free; zz++)
			{
				if (yy >= 0 && yy < Level::maxBuildHeight)
				{
					int tt = level->getTile(xx, yy, zz);
					if (tt != 0 && tt != Tile::goldenOakLeaves_Id && tt != Tile::skyrootLeaves_Id) free = false;
				}
				else
				{
					free = false;
				}
			}
		}
	}

	if (!free) return false;

	// Must stand on aether ground
	int belowTile = level->getTile(x, y - 1, z);
	if ((belowTile != Tile::aetherGrass_Id && belowTile != Tile::aetherDirt_Id) || y >= Level::maxBuildHeight - treeHeight - 1) return false;

	placeBlock(level, x, y - 1, z, Tile::aetherDirt_Id);

	// Place leaf canopy (tapered cone like spruce)
	int currentRadius = random->nextInt(2);
	int maxRadius = 1;
	int minRadius = 0;
	for (int heightPos = 0; heightPos <= topHeight; heightPos++)
	{
		const int yy = y + treeHeight - heightPos;

		for (int xx = x - currentRadius; xx <= x + currentRadius; xx++)
		{
			int xo = xx - (x);
			for (int zz = z - currentRadius; zz <= z + currentRadius; zz++)
			{
				int zo = zz - (z);
				if (abs(xo) == currentRadius && abs(zo) == currentRadius && currentRadius > 0) continue;
				if (!Tile::solid[level->getTile(xx, yy, zz)]) placeBlock(level, xx, yy, zz, Tile::goldenOakLeaves_Id, 0);
			}
		}

		if (currentRadius >= maxRadius)
		{
			currentRadius = minRadius;
			minRadius = 1;
			maxRadius += 1;
			if (maxRadius > leafRadius)
			{
				maxRadius = leafRadius;
			}
		}
		else
		{
			currentRadius = currentRadius + 1;
		}
	}

	// Place trunk
	int topOffset = random->nextInt(3);
	for (int hh = 0; hh < treeHeight - topOffset; hh++)
	{
		int t = level->getTile(x, y + hh, z);
		if (t == 0 || t == Tile::goldenOakLeaves_Id || t == Tile::skyrootLeaves_Id) placeBlock(level, x, y + hh, z, Tile::goldenOakLog_Id, 0);
	}

	return true;
}
