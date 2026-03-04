#include "stdafx.h"
#include "AerCloudFeature.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.h"

AerCloudFeature::AerCloudFeature(int tileId, int minRadius, int maxRadius, int minHeight, int maxHeight, bool islandBottom)
	: Feature(false)
{
	this->tileId = tileId;
	this->minRadius = minRadius;
	this->maxRadius = maxRadius;
	this->minHeight = minHeight;
	this->maxHeight = maxHeight;
	this->islandBottom = islandBottom;
}

int AerCloudFeature::findIslandBottom(Level *level, int x, int startY, int z)
{
	// Scan down from startY to find the lowest solid block in this column that's part of an island
	for (int yy = startY; yy >= 1; yy--)
	{
		int tile = level->getTile(x, yy, z);
		if (tile != 0)
		{
			// Found solid — now find the bottom of this solid section
			for (int by = yy; by >= 1; by--)
			{
				int belowTile = level->getTile(x, by - 1, z);
				if (belowTile == 0)
				{
					return by; // This is the bottom of the island
				}
			}
			return 1;
		}
	}
	return -1; // No solid block found
}

void AerCloudFeature::generateCloudBlob(Level *level, Random *random, int cx, int cy, int cz, int rx, int rz, int ry)
{
	// Generate an ellipsoidal cloud using distance checks with randomization for organic shape
	float rxSq = (float)(rx * rx);
	float rySq = (float)(ry * ry);
	float rzSq = (float)(rz * rz);

	for (int dx = -rx; dx <= rx; dx++)
	{
		for (int dz = -rz; dz <= rz; dz++)
		{
			for (int dy = -ry; dy <= ry; dy++)
			{
				float distSq = (dx * dx) / rxSq + (dy * dy) / rySq + (dz * dz) / rzSq;

				// Ellipsoid check with randomized edges for organic cloud shapes
				if (distSq < 1.0f + (random->nextFloat() * 0.3f - 0.15f))
				{
					int bx = cx + dx;
					int by = cy + dy;
					int bz = cz + dz;

					if (by < 1 || by >= Level::genDepth) continue;

					// Only place in air
					if (level->getTile(bx, by, bz) == 0)
					{
						placeBlock(level, bx, by, bz, tileId);
					}
				}
			}
		}
	}
}

bool AerCloudFeature::place(Level *level, Random *random, int x, int y, int z)
{
	if (islandBottom)
	{
		// Large cloud formation at the underside of islands
		// Find the bottom of the nearest island
		int bottomY = findIslandBottom(level, x, y, z);
		if (bottomY < 2) return false;

		// Verify there's actually an island above (at least a few solid blocks)
		int solidCount = 0;
		for (int cy = bottomY; cy <= bottomY + 5 && cy < Level::genDepth; cy++)
		{
			if (level->getTile(x, cy, z) != 0) solidCount++;
		}
		if (solidCount < 3) return false;

		// Verify there's open air below the island bottom
		bool hasAirBelow = false;
		for (int cy = bottomY - 1; cy >= bottomY - 4 && cy >= 0; cy--)
		{
			if (level->getTile(x, cy, z) == 0)
			{
				hasAirBelow = true;
				break;
			}
		}
		if (!hasAirBelow) return false;

		// Generate a large cloud clump beneath the island
		int rx = minRadius + random->nextInt(maxRadius - minRadius + 1);
		int rz = minRadius + random->nextInt(maxRadius - minRadius + 1);
		int ry = minHeight + random->nextInt(maxHeight - minHeight + 1);

		int cloudY = bottomY - ry - 1;
		if (cloudY < 1) return false;

		// Place multiple overlapping blobs for a more natural cloud appearance
		int blobCount = 2 + random->nextInt(3); // 2-4 overlapping blobs
		for (int i = 0; i < blobCount; i++)
		{
			int blobX = x + random->nextInt(rx) - rx / 2;
			int blobZ = z + random->nextInt(rz) - rz / 2;
			int blobY = cloudY + random->nextInt(ry > 1 ? ry : 1);
			int blobRx = rx / 2 + random->nextInt(rx / 2 + 1);
			int blobRz = rz / 2 + random->nextInt(rz / 2 + 1);
			int blobRy = max(1, ry / 2 + random->nextInt(max(1, ry / 2)));

			generateCloudBlob(level, random, blobX, blobY, blobZ, blobRx, blobRz, blobRy);
		}

		return true;
	}
	else
	{
		// Small floating cloud in the sky at the given position
		// Only place in air — verify the position is actually open
		if (level->getTile(x, y, z) != 0) return false;

		int rx = minRadius + random->nextInt(maxRadius - minRadius + 1);
		int rz = minRadius + random->nextInt(maxRadius - minRadius + 1);
		int ry = minHeight + random->nextInt(maxHeight - minHeight + 1);

		generateCloudBlob(level, random, x, y, z, rx, rz, ry);

		return true;
	}
}
