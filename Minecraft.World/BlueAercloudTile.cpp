#include "stdafx.h"
#include "BlueAercloudTile.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.entity.h"
#include "net.minecraft.world.h"

BlueAercloudTile::BlueAercloudTile(int id) : AercloudTile(id)
{
}

int BlueAercloudTile::getColor() const
{
	return 0xA3D1FF; // Blue
}

int BlueAercloudTile::getColor(int auxData)
{
	return 0xA3D1FF;
}

int BlueAercloudTile::getColor(LevelSource *level, int x, int y, int z)
{
	return 0xA3D1FF;
}

int BlueAercloudTile::getColor(LevelSource *level, int x, int y, int z, int data)
{
	return 0xA3D1FF;
}

void BlueAercloudTile::entityInside(Level *level, int x, int y, int z, shared_ptr<Entity> entity)
{
	// Slow horizontal movement like regular aercloud
	entity->xd *= 0.4;
	entity->zd *= 0.4;

	entity->fallDistance = 0;
	entity->yd = 1.25;

	// Spawn water splash particles at the entity's feet
	float bbWidth = entity->bbWidth;
	for (int i = 0; i < 8; i++)
	{
		float xo = (level->random->nextFloat() * 2 - 1) * bbWidth;
		float zo = (level->random->nextFloat() * 2 - 1) * bbWidth;
		level->addParticle(eParticleType_splash, entity->x + xo, entity->y, entity->z + zo, 0, 0.25, 0);
	}
}
