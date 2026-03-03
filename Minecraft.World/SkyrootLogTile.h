#pragma once
#include "Tile.h"

class Player;

class SkyrootLogTile : public Tile
{
	friend class Tile;
private:
	Icon *iconSide;
	Icon *iconTop;
	wstring sideTextureName;
	wstring topTextureName;

public:
	static const int MASK_FACING = 0xC;
	static const int FACING_Y = 0 << 2;
	static const int FACING_X = 1 << 2;
	static const int FACING_Z = 2 << 2;

	// Player-placed bit for logs uses bit 0 (bits 2-3 are used for rotation)
	static const int PLAYER_PLACED_BIT = 0x1;

protected:
	SkyrootLogTile(int id, const wstring &sideTexture = L"SkyrootLogSide", const wstring &topTexture = L"SkyrootLogTop");

public:
	virtual int getRenderShape();
	virtual Icon *getTexture(int face, int data);
	virtual void setPlacedBy(Level *level, int x, int y, int z, shared_ptr<Mob> by);
	virtual void playerDestroy(Level *level, shared_ptr<Player> player, int x, int y, int z, int data);
	void registerIcons(IconRegister *iconRegister);

protected:
	int getSpawnResourcesAuxValue(int data);
};
