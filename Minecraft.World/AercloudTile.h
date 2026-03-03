#pragma once
#include "HalfTransparentTile.h"

class AercloudTile : public HalfTransparentTile
{
	friend class Tile;
protected:
	AercloudTile(int id);

public:
	virtual AABB *getAABB(Level *level, int x, int y, int z);
	virtual void fallOn(Level *level, int x, int y, int z, shared_ptr<Entity> entity, float fallDistance);
	virtual void entityInside(Level *level, int x, int y, int z, shared_ptr<Entity> entity);
	virtual bool isSolidRender(bool isServerLevel = false);
};
