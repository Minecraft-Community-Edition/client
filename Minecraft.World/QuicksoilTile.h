#pragma once
#include "Tile.h"

class Player;

class QuicksoilTile : public Tile
{
	friend class Tile;

public:
	// Speed boost multiplier applied each tick while on quicksoil (exponential ramp-up)
	static const float BOOST_PER_TICK;
	// Maximum horizontal speed magnitude (2x normal walking speed of ~0.215 on standard ground)
	static const float MAX_SPEED;

protected:
	QuicksoilTile(int id);

public:
	virtual void stepOn(Level *level, int x, int y, int z, shared_ptr<Entity> entity);
	virtual void setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Entity> entity);
	virtual void playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data);
};
