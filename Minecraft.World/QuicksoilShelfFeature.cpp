#include "stdafx.h"
#include "QuicksoilShelfFeature.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"

QuicksoilShelfFeature::QuicksoilShelfFeature() : Feature(false)
{
}

bool QuicksoilShelfFeature::place(Level *level, Random *random, int x, int y, int z)
{
	// Find the bottom of the island at this XZ column
	// Start from the given Y and scan downward to find the lowest solid block
	int bottomY = -1;
	for (int yy = y; yy >= 1; yy--)
	{
		int tile = level->getTile(x, yy, z);
		if (tile != 0)
		{
			// Check if the block below is air — this is the island bottom
			if (level->getTile(x, yy - 1, z) == 0)
			{
				bottomY = yy;
				break;
			}
		}
	}

	if (bottomY < 1) return false;

	// Generate a shelf of quicksoil on the underside of the island
	// Place quicksoil in a roughly circular blob hanging from the bottom
	int radius = 2 + random->nextInt(3);  // radius 2-4
	int depth = 1 + random->nextInt(2);   // depth 1-2

	for (int dx = -radius; dx <= radius; dx++)
	{
		for (int dz = -radius; dz <= radius; dz++)
		{
			// Circular shape with some randomness
			float dist = sqrt((float)(dx * dx + dz * dz));
			if (dist > radius + 0.5f) continue;

			// More likely to place at center, less at edges
			if (dist > radius - 1 && random->nextInt(3) != 0) continue;

			int px = x + dx;
			int pz = z + dz;

			for (int dy = 0; dy < depth; dy++)
			{
				int py = bottomY - dy;
				if (py < 1) continue;

				int existing = level->getTile(px, py, pz);
				// Only replace air blocks or existing holystone/aetherDirt at the underside
				if (existing == 0 || existing == Tile::holystone_Id || existing == Tile::aetherDirt_Id)
				{
					level->setTileNoUpdate(px, py, pz, Tile::quicksoil_Id);
				}
			}
		}
	}

	return true;
}
