#include "stdafx.h"
#include "AetherMultiFaceTile.h"
#include "net.minecraft.world.level.h"
#include "Facing.h"
#include "IconRegister.h"

AetherMultiFaceTile::AetherMultiFaceTile(int id, const wstring &topTex, const wstring &sideTex) : Tile(id, Material::stone)
{
	iconTop = NULL;
	iconSide = NULL;
	texTop = topTex;
	texSide = sideTex;
}

Icon *AetherMultiFaceTile::getTexture(int face, int data)
{
	if (face == Facing::UP || face == Facing::DOWN) return iconTop;
	return iconSide;
}

void AetherMultiFaceTile::registerIcons(IconRegister *iconRegister)
{
	iconTop = iconRegister->registerIcon(texTop);
	iconSide = iconRegister->registerIcon(texSide);
}
