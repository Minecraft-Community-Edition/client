#include "stdafx.h"
#include "AercloudTile.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.phys.h"
#include "net.minecraft.world.h"

AercloudTile::AercloudTile(int id) : HalfTransparentTile(id, L"Aercloud", Material::cloth, false)
{
}

AABB *AercloudTile::getAABB(Level *level, int x, int y, int z)
{
	// Entities sink about 75% into the cloud (4/16 remaining)
	float r = 12 / 16.0f;
	return AABB::newTemp(x, y, z, x + 1, y + 1 - r, z + 1);
}

void AercloudTile::fallOn(Level *level, int x, int y, int z, shared_ptr<Entity> entity, float fallDistance)
{
	// Slow the entity's fall instead of bouncing
	entity->fallDistance = 0;
	if (entity->yd < 0)
	{
		entity->yd *= 0.1;
	}
}

void AercloudTile::entityInside(Level *level, int x, int y, int z, shared_ptr<Entity> entity)
{
	// Slow horizontal movement like soul sand
	entity->xd *= 0.4;
	entity->zd *= 0.4;

	// Slow falling speed for a floaty, airy feel
	if (entity->yd < 0)
	{
		entity->yd *= 0.5;
		entity->fallDistance = 0;
	}
}

bool AercloudTile::isSolidRender(bool isServerLevel)
{
	return false;
}
