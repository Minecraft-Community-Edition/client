#pragma once
#include "Tile.h"

class ChunkRebuildData;

class HalfTransparentTile : public Tile
{
	friend class ChunkRebuildData;
	friend class Tile;
private:
	bool allowSame;
	wstring texture;
protected:
	HalfTransparentTile(int id, const wstring &tex, Material *material, bool allowSame);
public:
    virtual bool isSolidRender(bool isServerLevel = false);
    virtual bool shouldRenderFace(LevelSource *level, int x, int y, int z, int face);
    virtual bool blocksLight();
    virtual bool isCubeShaped();
    virtual int getRenderLayer();
	void registerIcons(IconRegister *iconRegister);
};
