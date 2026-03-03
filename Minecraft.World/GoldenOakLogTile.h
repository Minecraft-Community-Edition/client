#pragma once
#include "SkyrootLogTile.h"

class Player;

class GoldenOakLogTile : public SkyrootLogTile
{
	friend class Tile;

protected:
	GoldenOakLogTile(int id, const wstring &sideTexture, const wstring &topTexture);

public:
	virtual int getResource(int data, Random *random, int playerBonusLevel);
	virtual void playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data);
};
