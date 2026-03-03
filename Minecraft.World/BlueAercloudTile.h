#pragma once
#include "AercloudTile.h"

class BlueAercloudTile : public AercloudTile
{
	friend class Tile;
protected:
	BlueAercloudTile(int id);

public:
	virtual int getColor() const;
	virtual int getColor(int auxData);
	virtual int getColor(LevelSource *level, int x, int y, int z);
	virtual int getColor(LevelSource *level, int x, int y, int z, int data);

	virtual void entityInside(Level *level, int x, int y, int z, shared_ptr<Entity> entity);
};
