#include "stdafx.h"
#include "QuicksoilShelfFeature.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"

QuicksoilShelfFeature::QuicksoilShelfFeature() : Feature(false)
{
}

static bool isIslandBlock(int tile)
{
	return tile == Tile::holystone_Id || tile == Tile::aetherDirt_Id;
}

bool QuicksoilShelfFeature::place(Level *level, Random *random, int x, int y, int z)
{
	// scan down to find holystone/aether dirt
	int placeY = -1;
	for (int yy = y; yy >= 1; yy--)
	{
		int tile = level->getTile(x, yy, z);
		if (isIslandBlock(tile))
		{
			placeY = yy;
			break;
		}
	}

	if (placeY < 1) return false;

	// make sure this block is actually exposed and not buried inside the island
	bool exposedToAir = false;
	int neighborOffsets[][3] = { {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,1,0} };
	for (int n = 0; n < 6; n++)
	{
		if (level->getTile(x + neighborOffsets[n][0], placeY + neighborOffsets[n][1], z + neighborOffsets[n][2]) == 0)
		{
			exposedToAir = true;
			break;
		}
	}
	if (!exposedToAir) return false;

	// need more island blocks above so we know this isn't just a tree or random block
	bool hasIslandNeighborAbove = false;
	for (int cy = placeY + 1; cy <= placeY + 3; cy++)
	{
		if (isIslandBlock(level->getTile(x, cy, z)) || level->getTile(x, cy, z) == Tile::aetherGrass_Id)
		{
			hasIslandNeighborAbove = true;
			break;
		}
	}
	if (!hasIslandNeighborAbove) return false;

	// check that we're actually near the edge of the island by looking
	// for open air in at least one cardinal direction
	const int scanDist = 8;
	const int airThreshold = 5;
	bool nearEdge = false;

	int dirX[] = { 0, 0, 1, -1 };
	int dirZ[] = { 1, -1, 0, 0 };

	for (int d = 0; d < 4; d++)
	{
		int airCount = 0;
		for (int i = 1; i <= scanDist; i++)
		{
			int sx = x + dirX[d] * i;
			int sz = z + dirZ[d] * i;

			// if this whole column is air, count it
			bool columnIsAir = true;
			for (int cy = placeY - 2; cy <= placeY + 2; cy++)
			{
				if (level->getTile(sx, cy, sz) != 0)
				{
					columnIsAir = false;
					break;
				}
			}

			if (columnIsAir)
				airCount++;
		}

		if (airCount >= airThreshold)
		{
			nearEdge = true;
			break;
		}
	}

	if (!nearEdge) return false;

	// bail out if there's already quicksoil nearby so we don't get clusters
	const int spacingRadius = 8;
	for (int sx = -spacingRadius; sx <= spacingRadius; sx++)
	{
		for (int sz = -spacingRadius; sz <= spacingRadius; sz++)
		{
			for (int sy = -3; sy <= 3; sy++)
			{
				if (level->getTile(x + sx, placeY + sy, z + sz) == Tile::quicksoil_Id)
					return false;
			}
		}
	}

	// place the shelf - flat quicksoil balcony at one Y level
	// grows outward each pass by attaching to existing blocks
	int shelfY = placeY;
	int radius = 3 + random->nextInt(4);
	int outwardPasses = 3 + random->nextInt(3);

	for (int pass = 0; pass < outwardPasses; pass++)
	{
		for (int dx = -radius; dx <= radius; dx++)
		{
			for (int dz = -radius; dz <= radius; dz++)
			{
				float dist = sqrt((float)(dx * dx + dz * dz));
				if (dist > radius + 0.5f) continue;

				// thin out towards the edges
				if (dist > radius - 1 && random->nextInt(3) != 0) continue;

				int px = x + dx;
				int pz = z + dz;

				if (level->getTile(px, shelfY, pz) != 0) continue;

				// only attach to adjacent solid blocks (sides + below, not above)
				bool canAttach = false;
				int hOffsets[][3] = { {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}, {0,-1,0} };
				for (int n = 0; n < 5; n++)
				{
					int neighbor = level->getTile(px + hOffsets[n][0], shelfY + hOffsets[n][1], pz + hOffsets[n][2]);
					if (isIslandBlock(neighbor) || neighbor == Tile::quicksoil_Id)
					{
						canAttach = true;
						break;
					}
				}

				if (canAttach)
				{
					level->setTileNoUpdate(px, shelfY, pz, Tile::quicksoil_Id);
				}
			}
		}
	}

	return true;
}
