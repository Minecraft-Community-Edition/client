#pragma once
#include "Tile.h"

class AetherMultiFaceTile : public Tile
{
	friend class Tile;
private:
	Icon *iconTop;
	Icon *iconSide;
	wstring texTop;
	wstring texSide;

protected:
	AetherMultiFaceTile(int id, const wstring &topTex, const wstring &sideTex);

public:
	virtual Icon *getTexture(int face, int data);
	void registerIcons(IconRegister *iconRegister);
};
