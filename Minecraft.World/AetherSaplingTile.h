#pragma once
#include "AetherBushTile.h"

class AetherSaplingTile : public AetherBushTile
{
	friend class Tile;
public:
	static const int AGE_BIT = 8;

protected:
	AetherSaplingTile(int id);

public:
	virtual void tick(Level *level, int x, int y, int z, Random *random);

	void growTree(Level *level, int x, int y, int z, Random *random);
};
