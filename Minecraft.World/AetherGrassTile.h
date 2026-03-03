#pragma once
#include "Tile.h"
#include "AetherNaturalTile.h"

class Level;
class ChunkRebuildData;

class AetherGrassTile : public Tile
{
	friend class Tile;
	friend class ChunkRebuildData;
private:
	Icon *iconTop;
	Icon *iconSide;

public:
	static const int MIN_BRIGHTNESS = 4;

protected:
	AetherGrassTile(int id);

public:
	virtual Icon *getTexture(int face, int data);
	virtual Icon *getTexture(LevelSource *level, int x, int y, int z, int face);
	void registerIcons(IconRegister *iconRegister);
	virtual void tick(Level *level, int x, int y, int z, Random *random);
	virtual int getResource(int data, Random *random, int playerBonusLevel);

	virtual void setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Mob> by);
	virtual void playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data);

	virtual bool shouldTileTick(Level *level, int x, int y, int z);
};
