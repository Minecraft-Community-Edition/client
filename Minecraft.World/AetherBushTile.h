#pragma once
#include "Bush.h"

class AetherBushTile : public Bush
{
	friend class Tile;
protected:
	AetherBushTile(int id);
	AetherBushTile(int id, Material *material);

	virtual bool mayPlaceOn(int tile);
};
