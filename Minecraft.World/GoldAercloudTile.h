#pragma once
#include "AercloudTile.h"

class GoldAercloudTile : public AercloudTile
{
	friend class Tile;
protected:
	GoldAercloudTile(int id);

public:
	virtual int getColor() const;
	virtual int getColor(int auxData);
	virtual int getColor(LevelSource *level, int x, int y, int z);
	virtual int getColor(LevelSource *level, int x, int y, int z, int data);
};
