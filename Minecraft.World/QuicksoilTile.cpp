#include "stdafx.h"
#include "QuicksoilTile.h"
#include "AetherNaturalTile.h"
#include "net.minecraft.world.entity.h"
#include "net.minecraft.world.entity.player.h"
#include "net.minecraft.world.level.h"
#include "Mth.h"

const float QuicksoilTile::BOOST_PER_TICK = 1.02f;
const float QuicksoilTile::MAX_SPEED = 0.43f;

QuicksoilTile::QuicksoilTile(int id) : Tile(id, Material::sand)
{
}

void QuicksoilTile::stepOn(Level *level, int x, int y, int z, shared_ptr<Entity> entity)
{
	if (!entity->onGround) return;

	double speed = Mth::sqrt(entity->xd * entity->xd + entity->zd * entity->zd);

	// Only boost if the entity is actually moving
	if (speed < 0.001) return;

	// Apply exponential boost
	double newSpeed = speed * BOOST_PER_TICK;

	// Cap at maximum speed (2x normal walking speed)
	if (newSpeed > MAX_SPEED)
		newSpeed = MAX_SPEED;

	// Scale the velocity components to preserve direction
	double scale = newSpeed / speed;
	entity->xd *= scale;
	entity->zd *= scale;
}

void QuicksoilTile::setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Entity> entity)
{
	int data = level->getData(x, y, z);
	level->setData(x, y, z, data | AetherNaturalTile::PLAYER_PLACED_BIT);
}

void QuicksoilTile::playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data)
{
	int cleanData = data & ~AetherNaturalTile::PLAYER_PLACED_BIT;
	bool isNatural = (data & AetherNaturalTile::PLAYER_PLACED_BIT) == 0;

	Tile::playerDestroy(level, player, x, y, z, cleanData);

	if (isNatural && AetherNaturalTile::isSkyrootTool(player))
	{
		AetherNaturalTile::spawnSkyrootBonusDrops(this, level, player, x, y, z, cleanData);
	}
}
