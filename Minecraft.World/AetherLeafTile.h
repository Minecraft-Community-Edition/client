#pragma once
#include "TransparentTile.h"

class Random;

class AetherLeafTile : public TransparentTile
{
	friend class Tile;
protected:
	AetherLeafTile(int id);

public:
	virtual int getResource(int data, Random *random, int playerBonusLevel);
	virtual int getResourceCount(Random *random);
	virtual bool isSeasonalLeaf() const;
};
