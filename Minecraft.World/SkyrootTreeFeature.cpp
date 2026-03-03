#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"
#include "SkyrootTreeFeature.h"

SkyrootTreeFeature::SkyrootTreeFeature(bool doUpdate) : Feature(doUpdate), baseHeight(4)
{
}

SkyrootTreeFeature::SkyrootTreeFeature(bool doUpdate, int baseHeight) : Feature(doUpdate), baseHeight(baseHeight)
{
}

bool SkyrootTreeFeature::place(Level *level, Random *random, int x, int y, int z)
{
	int treeHeight = random->nextInt(3) + baseHeight;

	bool free = true;
	if (y < 1 || y + treeHeight + 1 > Level::maxBuildHeight) return false;

	// 4J Stu Added to stop tree features generating areas previously placed by game rule generation
	if (app.getLevelGenerationOptions() != NULL)
	{
		PIXBeginNamedEvent(0, "SkyrootTreeFeature checking intersects");
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		bool intersects = levelGenOptions->checkIntersects(x - 2, y - 1, z - 2, x + 2, y + treeHeight, z + 2);
		PIXEndNamedEvent();
		if (intersects)
		{
			return false;
		}
	}

	for (int yy = y; yy <= y + 1 + treeHeight; yy++)
	{
		int r = 1;
		if (yy == y) r = 0;
		if (yy >= y + 1 + treeHeight - 2) r = 2;
		for (int xx = x - r; xx <= x + r && free; xx++)
		{
			for (int zz = z - r; zz <= z + r && free; zz++)
			{
				if (yy >= 0 && yy < Level::maxBuildHeight)
				{
					int tt = level->getTile(xx, yy, zz);
					if (tt != 0 && tt != Tile::skyrootLeaves_Id && tt != Tile::aetherGrass_Id && tt != Tile::aetherDirt_Id && tt != Tile::skyrootLog_Id) free = false;
				}
				else
				{
					free = false;
				}
			}
		}
	}

	if (!free) return false;

	int belowTile = level->getTile(x, y - 1, z);
	if ((belowTile != Tile::aetherGrass_Id && belowTile != Tile::aetherDirt_Id) || y >= Level::maxBuildHeight - treeHeight - 1) return false;

	placeBlock(level, x, y - 1, z, Tile::aetherDirt_Id, 0);

	PIXBeginNamedEvent(0, "Placing SkyrootTreeFeature leaves");
	int grassHeight = 3;
	int extraWidth = 0;
	// Generate leaves from the top down
	for (int yy = y + treeHeight; yy >= y - grassHeight + treeHeight; yy--)
	{
		int yo = yy - (y + treeHeight);
		int offs = extraWidth + 1 - yo / 2;
		for (int xx = x - offs; xx <= x + offs; xx++)
		{
			int xo = xx - (x);
			for (int zz = z - offs; zz <= z + offs; zz++)
			{
				int zo = zz - (z);
				if (abs(xo) == offs && abs(zo) == offs && (random->nextInt(2) == 0 || yo == 0)) continue;
				if (!Tile::solid[level->getTile(xx, yy, zz)]) placeBlock(level, xx, yy, zz, Tile::skyrootLeaves_Id, 0);
			}
		}
	}
	PIXEndNamedEvent();

	PIXBeginNamedEvent(0, "Placing SkyrootTreeFeature trunks");
	for (int hh = 0; hh < treeHeight; hh++)
	{
		int t = level->getTile(x, y + hh, z);
		if (t == 0 || t == Tile::skyrootLeaves_Id)
		{
			placeBlock(level, x, y + hh, z, Tile::skyrootLog_Id, 0);
		}
	}
	PIXEndNamedEvent();

	return true;
}
