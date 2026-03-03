#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.h"
#include "HalfTransparentTile.h"

HalfTransparentTile::HalfTransparentTile(int id, const wstring &tex, Material *material, bool allowSame) : Tile(id,material,isSolidRender())
{
	this->allowSame = allowSame;
	this->texture = tex;
}

bool HalfTransparentTile::isSolidRender(bool isServerLevel)
{
	return false;
}

bool HalfTransparentTile::shouldRenderFace(LevelSource *level, int x, int y, int z, int face)
{
    int id = level->getTile(x, y, z);
    if (!allowSame && id == this->id) return false;
    return Tile::shouldRenderFace(level, x, y, z, face);
}

bool HalfTransparentTile::blocksLight()
{
	return false;
}

bool HalfTransparentTile::isCubeShaped()
{
	return false;
}

int HalfTransparentTile::getRenderLayer()
{
	return 1;
}

void HalfTransparentTile::registerIcons(IconRegister *iconRegister)
{
	icon = iconRegister->registerIcon(texture);
}