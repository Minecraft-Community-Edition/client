#pragma once
#include "Tile.h"

class Random;
class Player;

class AetherOreTile : public Tile
{
public:
	AetherOreTile(int id);
	virtual int getResource(int data, Random *random, int playerBonusLevel);
	virtual int getResourceCount(Random *random);
	virtual void spawnResources(Level *level, int x, int y, int z, int data, float odds, int playerBonusLevel);
	virtual void setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Mob> by);
	virtual void playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data);
};
