#include "stdafx.h"
#include "net.minecraft.world.entity.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.dimension.h"
#include "net.minecraft.world.item.h"
#include "AetherPortalTile.h"

AetherPortalTile::AetherPortalTile(int id) : HalfTransparentTile(id, L"water", Material::portal, false)
{
	setTicking(true);
}

void AetherPortalTile::tick(Level *level, int x, int y, int z, Random *random)
{
	HalfTransparentTile::tick(level, x, y, z, random);
}

AABB *AetherPortalTile::getAABB(Level *level, int x, int y, int z)
{
	return NULL;
}

void AetherPortalTile::updateShape(LevelSource *level, int x, int y, int z, int forceData, shared_ptr<TileEntity> forceEntity)
{
	if (level->getTile(x - 1, y, z) == id || level->getTile(x + 1, y, z) == id)
	{
		float xr = 8 / 16.0f;
		float yr = 2 / 16.0f;
		this->setShape(0.5f - xr, 0, 0.5f - yr, 0.5f + xr, 1, 0.5f + yr);
	}
	else
	{
		float xr = 2 / 16.0f;
		float yr = 8 / 16.0f;
		this->setShape(0.5f - xr, 0, 0.5f - yr, 0.5f + xr, 1, 0.5f + yr);
	}
}

bool AetherPortalTile::isSolidRender(bool isServerLevel)
{
	return false;
}

bool AetherPortalTile::isCubeShaped()
{
	return false;
}

bool AetherPortalTile::trySpawnPortal(Level *level, int x, int y, int z, bool actuallySpawn)
{
	// The Aether portal uses a glowstone frame (like the Nether portal uses obsidian)
	int xd = 0, zd = 0;
	if (level->getTile(x - 1, y, z) == Tile::lightGem_Id || level->getTile(x + 1, y, z) == Tile::lightGem_Id) xd = 1;
	if (level->getTile(x, y, z - 1) == Tile::lightGem_Id || level->getTile(x, y, z + 1) == Tile::lightGem_Id) zd = 1;
	if (xd == zd) return false;

	if (level->getTile(x - xd, y, z - zd) == 0) { x -= xd; z -= zd; }

	for (int xx = -1; xx <= 2; xx++)
	{
		for (int yy = -1; yy <= 3; yy++)
		{
			bool edge = (xx == -1) || (xx == 2) || (yy == -1) || (yy == 3);
			if ((xx == -1 || xx == 2) && (yy == -1 || yy == 3)) continue;
			int t = level->getTile(x + xd * xx, y + yy, z + zd * xx);
			if (edge)
			{
				if (t != Tile::lightGem_Id) return false;
			}
			else
			{
				// Interior must be empty or water (since water bucket activates the portal)
				if (t != 0 && t != Tile::water_Id && t != Tile::calmWater_Id) return false;
			}
		}
	}

	if (!actuallySpawn) return true;

	level->noNeighborUpdate = true;
	for (int xx = 0; xx < 2; xx++)
	{
		for (int yy = 0; yy < 3; yy++)
		{
			level->setTile(x + xd * xx, y + yy, z + zd * xx, Tile::aetherPortalTile_Id);
		}
	}
	level->noNeighborUpdate = false;

	return true;
}

void AetherPortalTile::neighborChanged(Level *level, int x, int y, int z, int type)
{
	// Determine portal orientation from adjacent portal blocks
	int xd = 0;
	int zd = 1;
	if (level->getTile(x - 1, y, z) == id || level->getTile(x + 1, y, z) == id)
	{
		xd = 1;
		zd = 0;
	}

	// Scan down to find the bottom of the portal column
	int yBottom = y;
	while (level->getTile(x, yBottom - 1, z) == id)
		yBottom--;

	// Bottom must be glowstone
	if (level->getTile(x, yBottom - 1, z) != Tile::lightGem_Id)
	{
		level->setTile(x, y, z, 0);
		return;
	}

	// Portal column must be exactly 3 tall with glowstone on top
	int height = 1;
	while (height < 4 && level->getTile(x, yBottom + height, z) == id)
		height++;

	if (height != 3 || level->getTile(x, yBottom + height, z) != Tile::lightGem_Id)
	{
		level->setTile(x, y, z, 0);
		return;
	}

	// Don't allow cross-shaped portals
	bool we = level->getTile(x - 1, y, z) == id || level->getTile(x + 1, y, z) == id;
	bool ns = level->getTile(x, y, z - 1) == id || level->getTile(x, y, z + 1) == id;
	if (we && ns)
	{
		level->setTile(x, y, z, 0);
		return;
	}

	// One neighbor must be glowstone frame, the other must be portal
	if (!(
	(level->getTile(x + xd, y, z + zd) == Tile::lightGem_Id && level->getTile(x - xd, y, z - zd) == id) ||
	(level->getTile(x - xd, y, z - zd) == Tile::lightGem_Id && level->getTile(x + xd, y, z + zd) == id)
	))
	{
		level->setTile(x, y, z, 0);
		return;
	}
}

bool AetherPortalTile::shouldRenderFace(LevelSource *level, int x, int y, int z, int face)
{
	if (level->getTile(x, y, z) == id) return false;

	bool w = level->getTile(x - 1, y, z) == id && level->getTile(x - 2, y, z) != id;
	bool e = level->getTile(x + 1, y, z) == id && level->getTile(x + 2, y, z) != id;

	bool n = level->getTile(x, y, z - 1) == id && level->getTile(x, y, z - 2) != id;
	bool s = level->getTile(x, y, z + 1) == id && level->getTile(x, y, z + 2) != id;

	bool we = w || e;
	bool ns = n || s;

	if (we && face == 4) return true;
	if (we && face == 5) return true;
	if (ns && face == 2) return true;
	if (ns && face == 3) return true;

	return false;
}

int AetherPortalTile::getResourceCount(Random *random)
{
	return 0;
}

int AetherPortalTile::getRenderLayer()
{
	return 1;
}

void AetherPortalTile::entityInside(Level *level, int x, int y, int z, shared_ptr<Entity> entity)
{
	// Trigger portal teleportation — use the Aether portal flag
	if (entity->riding == NULL && entity->rider.lock() == NULL)
	{
		entity->handleInsideAetherPortal();
	}
}

void AetherPortalTile::animateTick(Level *level, int xt, int yt, int zt, Random *random)
{
	// No particles for the Aether portal
}

int AetherPortalTile::cloneTileId(Level *level, int x, int y, int z)
{
	return 0;
}

bool AetherPortalTile::mayPick()
{
	return false;
}
